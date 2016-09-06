#include "pch.h"
#include "BulletPhysics.h"
#include "LineRender.h"

#define BT_NO_SIMD_OPERATOR_OVERLOADS 1
#include "..\3rdParty\Bullet\src\btBulletCollisionCommon.h"
#include "..\3rdParty\Bullet\src\btBulletDynamicsCommon.h"

CollisionShape::CollisionShape( btCollisionShape* pShape )
    : m_pShape( pShape ),
      m_SweptSphereRadius( 0.0f )
{
    m_pShape->setUserPointer( this );
}

CollisionShape::~CollisionShape()
{
    delete m_pShape;
}

VOID CollisionShape::SetUniformScale( FLOAT Scale )
{
    m_pShape->setLocalScaling( btVector3( Scale, Scale, Scale ) );
}

CollisionShape* CollisionShape::CreateShape( const CollisionShapeDefinition* pDefinition )
{
    if( pDefinition == NULL )
    {
        return NULL;
    }

    switch( pDefinition->Type )
    {
    case CollisionShapeDefinition::Sphere:
        return CreateSphere( XMVectorGetX( pDefinition->vParams ) );
    case CollisionShapeDefinition::Box:
        return CreateBox( pDefinition->vParams );
    case CollisionShapeDefinition::Plane:
        return CreatePlane( pDefinition->vParams );
    case CollisionShapeDefinition::Capsule:
        return CreateCapsule( XMVectorGetX( pDefinition->vParams ), XMVectorGetY( pDefinition->vParams ) );
    }

    return NULL;
}

CollisionShape* CollisionShape::CreateBox( XMVECTOR vHalfSize )
{
    return new CollisionShape( new btBoxShape( *(btVector3*)&vHalfSize ) );
}

CollisionShape* CollisionShape::CreatePlane( XMVECTOR vPlane )
{
    return new CollisionShape( new btStaticPlaneShape( *(btVector3*)&vPlane, XMVectorGetW( vPlane ) ) );
}

CollisionShape* CollisionShape::CreateSphere( FLOAT Radius )
{
    CollisionShape* pShape = new CollisionShape( new btSphereShape( Radius ) );
    pShape->m_SweptSphereRadius = Radius;
    return pShape;
}

CollisionShape* CollisionShape::CreateCapsule( FLOAT Radius, FLOAT TotalHeight )
{
    FLOAT fBodyHeight = std::max( 0.0f, TotalHeight - Radius * 2.0f );
    return new CollisionShape( new btCapsuleShape( Radius, fBodyHeight ) );
}

CollisionShape* CollisionShape::CreateConvexHull( const XMFLOAT3* pPositionArray, DWORD PositionCount, DWORD StrideBytes )
{
    assert( StrideBytes >= sizeof(XMFLOAT3) );
    btConvexHullShape* pShape = new btConvexHullShape( (const btScalar*)pPositionArray, PositionCount, StrideBytes );
    return new CollisionShape( pShape );
}

CollisionShape* CollisionShape::CreateMesh( const XMFLOAT3* pPositionArray, DWORD PositionStrideBytes, DWORD VertexCount, const UINT* pTriangleListIndexArray, DWORD TriangleCount )
{
	assert( VertexCount > 0 );
	assert( TriangleCount > 0 );
	assert( PositionStrideBytes >= sizeof(XMFLOAT3) );

	XMFLOAT3* pVBCopy = new XMFLOAT3[VertexCount];
	const BYTE* pSrc = (const BYTE*)pPositionArray;
	for( DWORD i = 0; i < VertexCount; ++i )
	{
		pVBCopy[i] = *(const XMFLOAT3*)pSrc;
		pSrc += PositionStrideBytes;
	}

	INT* pIBCopy = new INT[TriangleCount * 3];
	memcpy( pIBCopy, pTriangleListIndexArray, TriangleCount * 3 * sizeof(UINT) );

	btTriangleIndexVertexArray* pTriangleArray = new btTriangleIndexVertexArray( TriangleCount, pIBCopy, 3 * sizeof(INT), VertexCount, (btScalar*)pVBCopy, sizeof(XMFLOAT3) );
	btBvhTriangleMeshShape* pTriangleShape = new btBvhTriangleMeshShape( pTriangleArray, TRUE );

    return new MeshCollisionShape( pTriangleShape, pTriangleArray, pVBCopy, pIBCopy );
}

MeshCollisionShape::~MeshCollisionShape()
{
	delete m_pTriangleArray;
	m_pTriangleArray = NULL;

	if( m_pVertexData != NULL )
	{
		delete[] m_pVertexData;
		m_pVertexData = NULL;
	}
	if( m_pIndexData != NULL )
	{
		delete[] m_pIndexData;
		m_pIndexData = NULL;
	}
}

CompoundCollisionShape::CompoundCollisionShape()
: CollisionShape( new btCompoundShape() )
{

}

VOID CompoundCollisionShape::AddShape( CollisionShape* pShape, CXMMATRIX matLocalTransform )
{
    btCompoundShape* pCompoundShape = (btCompoundShape*)m_pShape;
    btTransform Transform;
    Transform.setFromOpenGLMatrix( (FLOAT*)&matLocalTransform );
    pCompoundShape->addChildShape( Transform, pShape->GetInternalShape() );
}

Constraint::Constraint( RigidBody* pBodyA, RigidBody* pBodyB, btTypedConstraint* pConstraint )
    : m_pRigidBodyA( pBodyA ),
      m_pRigidBodyB( pBodyB ),
      m_pConstraint( pConstraint )
{
    m_pConstraint->setUserConstraintPtr( this );
}

Constraint::~Constraint()
{
    if( m_pWorld )
    {
        m_pWorld->RemoveConstraint( this );
    }
    delete m_pConstraint;
}

Constraint* Constraint::CreateBallSocket( RigidBody* pBodyA, RigidBody* pBodyB, FXMVECTOR vBallWorldPosition )
{
    XMVECTOR vDeterminant;
    XMMATRIX matInverseA = XMMatrixInverse( &vDeterminant, pBodyA->GetWorldTransform() );
    XMVECTOR vLocalPosA = XMVector3TransformCoord( vBallWorldPosition, matInverseA );
    btTypedConstraint* pConstraint = NULL;
    if( pBodyB != NULL )
    {
        XMMATRIX matInverseB = XMMatrixInverse( &vDeterminant, pBodyB->GetWorldTransform() );
        XMVECTOR vLocalPosB = XMVector3TransformCoord( vBallWorldPosition, matInverseB );
        pConstraint = new btPoint2PointConstraint( *pBodyA->GetInternalRigidBody(), *pBodyB->GetInternalRigidBody(), *(btVector3*)&vLocalPosA, *(btVector3*)&vLocalPosB );
    }
    else
    {
        pConstraint = new btPoint2PointConstraint( *pBodyA->GetInternalRigidBody(), *(btVector3*)&vLocalPosA );
    }
    return new Constraint( pBodyA, pBodyB, pConstraint );
}

Constraint* Constraint::CreateBallSocketMidpoint( RigidBody* pBodyA, RigidBody* pBodyB )
{
    XMVECTOR vCenterPoint = ( pBodyA->GetWorldPosition() + pBodyB->GetWorldPosition() ) * 0.5f;
    return CreateBallSocket( pBodyA, pBodyB, vCenterPoint );
}

RigidBody::RigidBody( CollisionShape* pShape, FLOAT Mass, CXMMATRIX matTransform )
{
    btVector3 LocalInertia( 0, 0, 0 );
    if( Mass > 0 )
    {
        pShape->GetInternalShape()->calculateLocalInertia( Mass, LocalInertia );
    }
    Initialize( pShape, Mass, matTransform, *(XMVECTOR*)&LocalInertia );
}

RigidBody::RigidBody( CollisionShape* pShape, FLOAT Mass, CXMMATRIX matTransform, CXMVECTOR vLocalInertia )
{
    Initialize( pShape, Mass, matTransform, vLocalInertia );
}

VOID RigidBody::Initialize( CollisionShape* pShape, FLOAT Mass, CXMMATRIX matTransform, CXMVECTOR vLocalInertia )
{
    btTransform Transform;
    XMMATRIX matFinal = matTransform; 
    Transform.setFromOpenGLMatrix( (FLOAT*)&matFinal );
    m_pMotionState = new btDefaultMotionState( Transform );
    btRigidBody::btRigidBodyConstructionInfo RBInfo( Mass, m_pMotionState, pShape->GetInternalShape(), *(btVector3*)&vLocalInertia );

    m_pRigidBody = new btRigidBody( RBInfo );
    m_pRigidBody->setUserPointer( this );
    if( Mass > 0 )
    {
        m_pRigidBody->setCcdSweptSphereRadius( pShape->GetSweptSphereRadius() );
        m_pRigidBody->setCcdMotionThreshold( pShape->GetSweptSphereRadius() * 0.5f );
    }

    m_pContactPoints = NULL;
    m_dwTotalContactPoints = 0;
    m_dwActiveContactPoints = 0;
    m_CollisionMask = 0;

    m_pWorld = NULL;
    m_strCollisionCallbackName[0] = '\0';
}

RigidBody::~RigidBody()
{
    m_pRigidBody->setUserPointer( nullptr );
    if( m_pWorld != NULL )
    {
        m_pWorld->RemoveRigidBody( this );
    }
    delete m_pMotionState;
    delete m_pRigidBody;
}

XMMATRIX RigidBody::GetWorldTransform() const
{
    btTransform Transform;
    if( IsDynamic() )
    {
        Transform = m_pRigidBody->getWorldTransform();
    }
    else
    {
        m_pMotionState->getWorldTransform( Transform );
    }
    XMMATRIX matTransform;
    Transform.getOpenGLMatrix( (FLOAT*)&matTransform );
    return matTransform;
}

XMVECTOR RigidBody::GetWorldPosition() const
{
    btTransform Transform;
    if( IsDynamic() )
    {
        Transform = m_pRigidBody->getWorldTransform();
    }
    else
    {
        m_pMotionState->getWorldTransform( Transform );
    }
    btVector3& Position = Transform.getOrigin();
    return *(XMVECTOR*)&Position;
}

XMVECTOR RigidBody::GetLinearVelocity() const
{
    btVector3 LinearVelocity = m_pRigidBody->getLinearVelocity();
    return *(XMVECTOR*)&LinearVelocity;
}

XMVECTOR RigidBody::GetAngularVelocity() const
{
    btVector3 AngularVelocity = m_pRigidBody->getAngularVelocity();
    return *(XMVECTOR*)&AngularVelocity;
}

VOID RigidBody::ApplyLinearImpulse( CXMVECTOR vLinearImpulse )
{
    m_pRigidBody->applyCentralImpulse( *(btVector3*)&vLinearImpulse );
    Activate();
}

VOID RigidBody::ApplyTorqueImpulse( CXMVECTOR vTorqueImpulse )
{
    m_pRigidBody->applyTorqueImpulse( *(btVector3*)&vTorqueImpulse );
    Activate();
}

VOID RigidBody::ApplyLocalTorqueImpulse( CXMVECTOR vTorqueImpulse )
{
    btVector3 localTorque = *(btVector3*)&vTorqueImpulse;
    btVector3 worldTorque = m_pRigidBody->getInvInertiaTensorWorld().inverse() * ( m_pRigidBody->getWorldTransform().getBasis() * localTorque );
    m_pRigidBody->applyTorqueImpulse( worldTorque );
    Activate();
}

VOID RigidBody::ApplyLocalTorque( CXMVECTOR vTorque )
{
    btVector3 localTorque = *(btVector3*)&vTorque;
    btVector3 worldTorque = m_pRigidBody->getInvInertiaTensorWorld().inverse() * ( m_pRigidBody->getWorldTransform().getBasis() * localTorque );
    m_pRigidBody->applyTorque( worldTorque );
    Activate();
}

VOID RigidBody::ApplyWorldTorque(CXMVECTOR vTorque)
{
    btVector3 worldTorque = *(btVector3*)&vTorque;
    m_pRigidBody->applyTorque(worldTorque);
    Activate();
}

FLOAT RigidBody::GetInverseMass() const
{
    return m_pRigidBody->getInvMass();
}

inline XMVECTOR XMQuaternionToRollPitchYaw( FXMVECTOR Rotation )
{
    XMVECTOR v02 = XMVectorSwizzle( Rotation, 0, 2, 0, 0 );
    XMVECTOR v13 = XMVectorSwizzle( Rotation, 1, 3, 0, 0 );
    XMVECTOR v12 = XMVectorSwizzle( Rotation, 1, 2, 0, 0 );
    XMVECTOR v03 = XMVectorSwizzle( Rotation, 0, 3, 0, 0 );
    XMVECTOR v21 = XMVectorSwizzle( Rotation, 2, 1, 0, 0 );
    XMVECTOR v32 = XMVectorSwizzle( Rotation, 3, 2, 0, 0 );
    XMVECTOR v23 = XMVectorSwizzle( Rotation, 2, 3, 0, 0 );
    XMVECTOR v01 = Rotation;

    FLOAT PitchA = XMVectorGetX( XMVector2Dot( v03, v21 * XMVectorSet( 1, -1, 0, 0 ) ) );
    FLOAT RollA = XMVectorGetX( XMVector2Dot( v02, v13 ) );
    FLOAT RollB = XMVectorGetX( XMVector2Dot( v12, v12 ) );
    FLOAT YawA = XMVectorGetX( XMVector2Dot( v01, v32 ) );
    FLOAT YawB = XMVectorGetX( XMVector2Dot( v23, v23 ) );

    FLOAT Roll = atan2f( 2.0f * RollA, 1.0f - 2.0f * RollB );
    FLOAT Yaw = atan2f( 2.0f * YawA, 1.0f - 2.0f * YawB );
    FLOAT Pitch = asinf( 2.0f * PitchA );

    return XMVectorSet( Roll, Pitch, Yaw, 0.0f );
}

VOID RigidBody::LerpTo( CXMVECTOR vDesiredPos, CXMVECTOR vDesiredOrientation, FLOAT DeltaTime )
{
    //DeltaTime = min( 1.0f, DeltaTime / 5.0f );
    const XMVECTOR vDeltaTime = XMVectorReplicate( DeltaTime );
    const XMVECTOR vInverseDeltaTime = XMVectorReciprocalEst( vDeltaTime );

    XMMATRIX CurrentTransform = GetWorldTransform();
    XMVECTOR CurrentPos = CurrentTransform.r[3];

    XMVECTOR CurrentLinearVelocity = GetLinearVelocity();
    XMVECTOR ProjectedPos = CurrentLinearVelocity * vDeltaTime + CurrentPos;
    XMVECTOR DeltaPos = vDesiredPos - ProjectedPos;
    XMVECTOR DesiredLinearVelocity = DeltaPos * vInverseDeltaTime;

    XMVECTOR vLinearImpulse = DesiredLinearVelocity - CurrentLinearVelocity;
    ApplyLinearImpulse( vLinearImpulse * vDeltaTime );

    XMVECTOR CurrentOrientation = XMQuaternionRotationMatrix( CurrentTransform );
    XMVECTOR CurrentAngularVelocity = XMQuaternionRotationRollPitchYawFromVector( GetAngularVelocity() );

    XMVECTOR ProjectedOrientation = XMQuaternionSlerpV( CurrentOrientation, XMQuaternionMultiply( CurrentOrientation, CurrentAngularVelocity ), vDeltaTime );
    XMVECTOR DeltaOrientation = XMQuaternionMultiply( vDesiredOrientation, XMQuaternionInverse( ProjectedOrientation ) );
    XMVECTOR OrientationImpulse = XMQuaternionSlerpV( XMQuaternionIdentity(), DeltaOrientation, vDeltaTime );
    
    ApplyTorqueImpulse( XMQuaternionToRollPitchYaw( OrientationImpulse ) );
}

VOID RigidBody::LerpTo( CXMMATRIX DesiredTransform, FLOAT DeltaTime )
{
    XMVECTOR qRotation = XMQuaternionRotationMatrix( DesiredTransform );
    LerpTo( DesiredTransform.r[3], qRotation, DeltaTime );
}

VOID RigidBody::SetWorldTransform( CXMMATRIX matTransform )
{
    btTransform Transform;
    Transform.setFromOpenGLMatrix( (FLOAT*)&matTransform );

    if( IsDynamic() )
    {
        m_pRigidBody->setWorldTransform( Transform );
        m_pRigidBody->clearForces();
        btVector3 zero( 0, 0, 0 );
        m_pRigidBody->setLinearVelocity( zero );
        m_pRigidBody->setAngularVelocity( zero );
    }
    else if( IsStatic() )
    {
        m_pRigidBody->getMotionState()->setWorldTransform( Transform );
        m_pRigidBody->setWorldTransform( Transform );
    }
    else
    {
        m_pRigidBody->getMotionState()->setWorldTransform( Transform );
    }
}

VOID RigidBody::SetDynamic()
{
    assert( !IsStatic() );
    m_pRigidBody->setCollisionFlags( m_pRigidBody->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT );
    m_pRigidBody->forceActivationState( ACTIVE_TAG );
}

VOID RigidBody::SetKinematic()
{
    m_pRigidBody->setCollisionFlags( m_pRigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT );
    m_pRigidBody->setActivationState( DISABLE_DEACTIVATION );
}

VOID RigidBody::SetStatic()
{
    m_pRigidBody->setCollisionFlags( btCollisionObject::CF_STATIC_OBJECT );
    m_pRigidBody->setActivationState( WANTS_DEACTIVATION );
}

VOID RigidBody::EnableCollisions( bool Enable )
{
    int flags = m_pRigidBody->getCollisionFlags();
    if (Enable)
    {
        flags &= ~btCollisionObject::CF_NO_CONTACT_RESPONSE;
    }
    else
    {
        flags |= btCollisionObject::CF_NO_CONTACT_RESPONSE;
    }
    m_pRigidBody->setCollisionFlags( flags );
}

VOID RigidBody::DisableDeactivation()
{
    m_pRigidBody->setActivationState( DISABLE_DEACTIVATION );
}

VOID RigidBody::EnableKinematicCollisions()
{
    if( m_pRigidBody->getBroadphaseHandle() != nullptr )
    {
        m_pRigidBody->getBroadphaseHandle()->m_collisionFilterMask = btBroadphaseProxy::AllFilter;
    }
}

BOOL RigidBody::IsDynamic() const
{
    return !m_pRigidBody->isStaticOrKinematicObject();
}

BOOL RigidBody::IsStatic() const
{
    return m_pRigidBody->isStaticObject();
}

BOOL RigidBody::IsActive() const
{
    return m_pRigidBody->isActive();
}

VOID RigidBody::Activate()
{
    m_pRigidBody->activate();
}

VOID RigidBody::ClearForces()
{
    m_pRigidBody->clearForces();
}

VOID RigidBody::ClearLinearVelocity()
{
    m_pRigidBody->setLinearVelocity( btVector3( 0, 0, 0 ) );
}

VOID RigidBody::ClearAngularVelocity()
{
    m_pRigidBody->setAngularVelocity( btVector3( 0, 0, 0 ) );
}

VOID RigidBody::ClearContactPoints()
{
    if( IsTrackingContactPoints() )
    {
        m_dwActiveContactPoints = 0;
    }
}

VOID RigidBody::TrackContactPoints( DWORD dwCount )
{
    if( dwCount > m_dwTotalContactPoints )
    {
        if( m_pContactPoints != NULL )
        {
            delete[] m_pContactPoints;
        }
        m_dwTotalContactPoints = dwCount;
        m_pContactPoints = new ContactPoint[dwCount];
    }
    else if( dwCount == 0 )
    {
        if( m_pContactPoints != NULL )
        {
            delete[] m_pContactPoints;
            m_pContactPoints = nullptr;
        }
        m_dwTotalContactPoints = 0;
    }
    m_dwActiveContactPoints = 0;
}

BOOL RigidBody::AddContactPoint( CXMVECTOR vLocalPos, CXMVECTOR Distance, RigidBody* pOtherRigidBody )
{
    if( !IsTrackingContactPoints() )
    {
        return FALSE;
    }
    if( m_dwActiveContactPoints >= m_dwTotalContactPoints )
    {
        return FALSE;
    }
    if( pOtherRigidBody != nullptr && !TestCollisionMask( pOtherRigidBody ) )
    {
        return FALSE;
    }

    DWORD i = m_dwActiveContactPoints;
    ++m_dwActiveContactPoints;

    m_pContactPoints[i].pOtherBody = pOtherRigidBody;
    XMStoreFloat3( &m_pContactPoints[i].vLocalPosition, vLocalPos );
    XMStoreFloat3( &m_pContactPoints[i].vWorldDirection, Distance );

    return TRUE;
}

XMVECTOR RigidBody::ShootRay( CXMVECTOR Direction, BOOL* pHit )
{
    return GetPhysicsWorld()->ShootRay( this, Direction, pHit );
}

VOID RigidBody::SetCollisionCallbackName( const CHAR* strCallback )
{
    if( strCallback == nullptr )
    {
        strCallback = "";
    }
    strcpy_s( m_strCollisionCallbackName, strCallback );
}

/*
VOID RigidBody::PerformCollisionCallback( LuaContext* pContext, ItemInstance* pII )
{
    if( !IsCollisionCallbackEnabled() )
    {
        return;
    }

    assert( pContext != nullptr );
    assert( pII == GetUserData() );

    for( UINT i = 0; i < m_dwActiveContactPoints; ++i )
    {
        const ContactPoint& CP = m_pContactPoints[i];
        const RigidBody* pOtherRB = CP.pOtherBody;
        if( pOtherRB == nullptr )
        {
            continue;
        }

        const ItemInstance* pOtherII = (const ItemInstance*)pOtherRB->GetUserData();
        if( pOtherII != nullptr && pOtherII->IsAlive() )
        {
            //DebugSpew( "collision between %S and %S\n", pII->GetItem()->GetName().GetSafeString(), pOtherII->GetItem()->GetName().GetSafeString() );
            const XMFLOAT3& LocalPos = CP.vLocalPosition;
            pContext->ExecuteFunctionWithUserdata( m_strCollisionCallbackName, pII, pOtherII, LocalPos.x, LocalPos.y, LocalPos.z );
        }
    }
}
*/

bool RigidBody::ApplyControls2D(FLOAT DeltaTime, FLOAT ForwardSpeed, FLOAT StrafeSpeed, FLOAT YawSpeed, FLOAT MaxSpeed, FLOAT MaxTurnSpeed)
{
    if (IsStatic())
    {
        return false;
    }

    const XMVECTOR InvDeltaTime = XMVectorReplicate(1.0f / DeltaTime);
    const XMMATRIX matWorld = GetWorldTransform();
    const XMVECTOR vPos = matWorld.r[3];
    const XMVECTOR vForward = matWorld.r[2];
    const XMVECTOR vRight = matWorld.r[0];

    XMVECTOR vNewPos = vPos;
    vNewPos += vForward * XMVectorReplicate(MaxSpeed * ForwardSpeed * DeltaTime);
    vNewPos += vRight * XMVectorReplicate(MaxSpeed * StrafeSpeed * DeltaTime);
    vNewPos += vPos * XMVectorSet(0, -1, 0, 0);

    const XMVECTOR DesiredVelocity = (vNewPos - vPos) * InvDeltaTime;
    const XMVECTOR CurrentVelocity = GetLinearVelocity();
    XMVECTOR LinearImpulse = DesiredVelocity - CurrentVelocity;
    ApplyLinearImpulse(LinearImpulse);

    ClearAngularVelocity();
    if (YawSpeed != 0.0f)
    {
        FLOAT DeltaYaw = YawSpeed * DeltaTime;
        XMVECTOR Torque = XMVectorSet(0, DeltaYaw, 0, 0) * MaxTurnSpeed;
        ApplyTorqueImpulse(Torque);
    }

    return true;
}

class PhysicsDebugDrawer : public btIDebugDraw
{
protected:
    INT m_DebugMode;

public:
    PhysicsDebugDrawer()
    {
        m_DebugMode = btIDebugDraw::DBG_DrawWireframe |
                      btIDebugDraw::DBG_DrawContactPoints |
                      btIDebugDraw::DBG_DrawAabb |
                      btIDebugDraw::DBG_DrawConstraints;
    }


    virtual void drawLine(const btVector3& from,const btVector3& to,const btVector3& color)
    {
        XMFLOAT4 Color = *(XMFLOAT4*)&color;
        Color.w = 1.0f;
        XMVECTOR vColor = XMLoadFloat4( &Color );
        XMVECTOR vFrom = XMLoadFloat3( (XMFLOAT3*)&from );
        XMVECTOR vTo = XMLoadFloat3( (XMFLOAT3*)&to );
        LineRender::DrawLine(vFrom, vTo, vColor);
    }

	virtual void drawContactPoint(const btVector3& PointOnB,const btVector3& normalOnB,btScalar distance,int lifeTime,const btVector3& color)
    {
        XMFLOAT4 Color = *(XMFLOAT4*)&color;
        Color.w = 1.0f;
        XMVECTOR vColor = XMLoadFloat4( &Color );
        XMVECTOR vPos = XMLoadFloat3( (XMFLOAT3*)&PointOnB );

        XMMATRIX matWorld = XMMatrixScalingFromVector( XMVectorReplicate( 0.05f ) ) * XMMatrixTranslationFromVector( vPos );

        LineRender::DrawCube(matWorld, vColor);
    }

	virtual void reportErrorWarning(const char* warningString)
    {
        OutputDebugStringA( warningString );
    }

	virtual void draw3dText(const btVector3& location,const char* textString)
    {
    }
	
    virtual void setDebugMode(int debugMode) { m_DebugMode = debugMode; }
    virtual int getDebugMode() const { return m_DebugMode; }

    VOID FinishRendering()
    {
    }
};

PhysicsDebugDrawer g_PhysicsDebugDrawer;

PhysicsWorld::PhysicsWorld()
    : m_pCollisionConfiguration(nullptr),
      m_pDispatcher(nullptr),
      m_pOverlappingPairCache(nullptr),
      m_pSolver(nullptr),
      m_pDynamicsWorld(nullptr)
{
}

void PhysicsWorld::Initialize(DWORD Flags, XMVECTOR vGravity)
{
    m_Speed = 1.0f;
    m_pCollisionConfiguration = new btDefaultCollisionConfiguration();
    m_pDispatcher = new btCollisionDispatcher(m_pCollisionConfiguration);
    m_pOverlappingPairCache = new btDbvtBroadphase();
    m_pSolver = new btSequentialImpulseConstraintSolver();
    m_pDynamicsWorld = new btDiscreteDynamicsWorld(m_pDispatcher, m_pOverlappingPairCache, m_pSolver, m_pCollisionConfiguration);
    m_pDynamicsWorld->setWorldUserInfo(this);

    m_pDynamicsWorld->setGravity(*(btVector3*)&vGravity);
    m_pDynamicsWorld->setDebugDrawer(&g_PhysicsDebugDrawer);
    m_pDynamicsWorld->setInternalTickCallback(SimulationTickCallback);
    m_pDynamicsWorld->getDispatchInfo().m_useContinuous = true;
}

PhysicsWorld::~PhysicsWorld()
{
    INT Count = m_pDynamicsWorld->getNumCollisionObjects();
    for( INT i = ( Count - 1 ); i >= 0; --i )
    {
        btCollisionObject* pObj = m_pDynamicsWorld->getCollisionObjectArray()[ i ];
        btRigidBody* pRB = btRigidBody::upcast( pObj );
        RigidBody* pRigidBody = (RigidBody*)pRB->getUserPointer();
        RemoveRigidBody( pRigidBody );
    }

    Count = m_pDynamicsWorld->getNumConstraints();
    for( INT i = ( Count - 1 ); i >= 0; --i )
    {
        btTypedConstraint* pObj = m_pDynamicsWorld->getConstraint( i );
        Constraint* pConstraint = (Constraint*)pObj->getUserConstraintPtr();
        RemoveConstraint( pConstraint );
    }

    {
        PhysicsObjectSet::iterator iter = m_OwnedObjects.begin();
        PhysicsObjectSet::iterator end = m_OwnedObjects.end();
        while( iter != end )
        {
            delete *iter;
            ++iter;
        }
        m_OwnedObjects.clear();
    }

    {
        CollisionShapeSet::iterator iter = m_OwnedShapes.begin();
        CollisionShapeSet::iterator end = m_OwnedShapes.end();
        while( iter != end )
        {
            delete *iter;
            ++iter;
        }
        m_OwnedShapes.clear();
    }

    delete m_pDynamicsWorld;
    delete m_pSolver;
    delete m_pOverlappingPairCache;
    delete m_pDispatcher;
    delete m_pCollisionConfiguration;
}

VOID PhysicsWorld::Update( FLOAT DeltaTime )
{
    ClearContactPoints();
    m_pDynamicsWorld->setWorldUserInfo( this );
    if( m_Speed > 0.0f )
    {
        m_pDynamicsWorld->stepSimulation( DeltaTime * m_Speed, 6 );
    }
}

VOID PhysicsWorld::SimulationTickCallback( btDynamicsWorld* pDynamicsWorld, FLOAT timeStep )
{
    PhysicsWorld* pPW = (PhysicsWorld*)pDynamicsWorld->getWorldUserInfo();
    assert( pPW != nullptr );
    pPW->ReportContactPoints();
}

VOID PhysicsWorld::DebugRender()
{
    m_pDynamicsWorld->debugDrawWorld();
}

VOID PhysicsWorld::ClearContactPoints()
{
    INT RBCount = m_pDynamicsWorld->getNumCollisionObjects();
    for( INT i = 0; i < RBCount; ++i )
    {
        btCollisionObject* pObj = m_pDynamicsWorld->getCollisionObjectArray()[ i ];
        btRigidBody* pRB = btRigidBody::upcast( pObj );
        if( pRB != NULL && pRB->getUserPointer() != NULL )
        {
            RigidBody* pRigidBody = (RigidBody*)pRB->getUserPointer();
            pRigidBody->ClearContactPoints();
        }
    }
}

VOID PhysicsWorld::ReportContactPoints()
{
    INT iNumContactManifolds = m_pDispatcher->getNumManifolds();
    for( INT i = 0; i < iNumContactManifolds; ++i )
    {
        const btPersistentManifold* pManifold = m_pDispatcher->getManifoldByIndexInternal( i );
        const INT numContacts = pManifold->getNumContacts();
        if( numContacts <= 0 )
            continue;

        const btCollisionObject* pObjA = static_cast<const btCollisionObject*>( pManifold->getBody0() );
        const btCollisionObject* pObjB = static_cast<const btCollisionObject*>( pManifold->getBody1() );

        RigidBody* pRigidBodyA = (RigidBody*)pObjA->getUserPointer();
        RigidBody* pRigidBodyB = (RigidBody*)pObjB->getUserPointer();

        if( pRigidBodyA == nullptr || pRigidBodyB == nullptr )
        {
            continue;
        }

        for( INT ContactIndex = 0; ContactIndex < numContacts; ++ContactIndex )
        {
            const btManifoldPoint& ContactPoint = pManifold->getContactPoint( ContactIndex );
            btVector3 btdistance = ContactPoint.m_normalWorldOnB * ContactPoint.m_distance1;
            XMVECTOR Distance = *(XMVECTOR*)&btdistance;
            if( pRigidBodyA != NULL )
            {
                pRigidBodyA->AddContactPoint( *(XMVECTOR*)&ContactPoint.m_localPointA, -Distance, pRigidBodyB );
            }
            if( pRigidBodyB != NULL )
            {
                pRigidBodyB->AddContactPoint( *(XMVECTOR*)&ContactPoint.m_localPointB, Distance, pRigidBodyA );
            }
        }
    }
}

// VOID PhysicsWorld::DebugDraw( ::D3DDevice* pd3dDevice )
// {
//     pd3dDevice->SetRenderState( D3DRS_ZENABLE, TRUE );
//     pd3dDevice->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
//     pd3dDevice->SetRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
//     pd3dDevice->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
//     pd3dDevice->SetRenderState( D3DRS_CULLMODE, D3DCULL_CCW );
// 
//     g_PhysicsDebugDrawer.SetDevice( pd3dDevice );
//     m_pDynamicsWorld->debugDrawWorld();
// }

VOID PhysicsWorld::OwnObject( PhysicsObject* pObject )
{
    m_OwnedObjects.insert( pObject );
}

VOID PhysicsWorld::OwnShape( CollisionShape* pShape )
{
    m_OwnedShapes.insert( pShape );
}

VOID PhysicsWorld::AddRigidBody( RigidBody* pRigidBody, BOOL WorldOwn )
{
    m_pDynamicsWorld->addRigidBody( pRigidBody->GetInternalRigidBody() );
    pRigidBody->SetPhysicsWorld( this );
    if( WorldOwn )
    {
        OwnObject( pRigidBody );
    }
}

VOID PhysicsWorld::RemoveRigidBody( RigidBody* pRigidBody )
{
    m_pDynamicsWorld->removeRigidBody( pRigidBody->GetInternalRigidBody() );
    pRigidBody->SetPhysicsWorld( NULL );
}

VOID PhysicsWorld::AddConstraint( Constraint* pConstraint, BOOL WorldOwn )
{
    m_pDynamicsWorld->addConstraint( pConstraint->GetInternalConstraint() );
    pConstraint->SetPhysicsWorld( this );
    if( WorldOwn )
    {
        OwnObject( pConstraint );
    }
}

VOID PhysicsWorld::RemoveConstraint( Constraint* pConstraint )
{
    m_pDynamicsWorld->removeConstraint( pConstraint->GetInternalConstraint() );
    pConstraint->SetPhysicsWorld( NULL );
}

class ClosestWithExclusion : public btCollisionWorld::ClosestRayResultCallback
{
public:
    ClosestWithExclusion (btRigidBody* me) : btCollisionWorld::ClosestRayResultCallback(btVector3(0.0, 0.0, 0.0), btVector3(0.0, 0.0, 0.0))
    {
        m_me = me;
    }

    virtual btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult,bool normalInWorldSpace)
    {
        if (rayResult.m_collisionObject == m_me)
            return 1.0;

        return ClosestRayResultCallback::addSingleResult (rayResult, normalInWorldSpace);
    }
protected:
    btRigidBody* m_me;
};

XMVECTOR PhysicsWorld::ShootRay( RigidBody* pRigidBody, CXMVECTOR Ray, BOOL* pHit )
{
    btRigidBody* pRB = pRigidBody->GetInternalRigidBody();
    ClosestWithExclusion NotMe( pRB );
    NotMe.m_closestHitFraction = 1.0f;
    XMVECTOR RayStart = pRigidBody->GetWorldPosition();
    XMVECTOR RayEnd = RayStart + Ray;
    m_pDynamicsWorld->rayTest( *(btVector3*)&RayStart, *(btVector3*)&RayEnd, NotMe );

    const BOOL HasHit = NotMe.hasHit();
    if( pHit != nullptr )
    {
        *pHit = HasHit;
    }

    if( HasHit )
    {
        return XMVectorLerp( RayStart, RayEnd, NotMe.m_closestHitFraction );
    }
    else
    {
        return RayEnd;
    }
}

XMVECTOR PhysicsWorld::ShootRay( CXMVECTOR Origin, CXMVECTOR Ray, BOOL* pHit, RigidBody** ppHitRigidBody )
{
    ClosestWithExclusion Callback( nullptr );
    Callback.m_closestHitFraction = 1.0f;
    XMVECTOR RayStart = Origin;
    XMVECTOR RayEnd = RayStart + Ray;
    m_pDynamicsWorld->rayTest( *(btVector3*)&RayStart, *(btVector3*)&RayEnd, Callback );

    const BOOL HasHit = Callback.hasHit();
    if( pHit != nullptr )
    {
        *pHit = HasHit;
    }

    if( HasHit )
    {
        auto* pCollisionObject = Callback.m_collisionObject;
        if( pCollisionObject != nullptr && ppHitRigidBody != nullptr )
        {
            RigidBody* pRB = (RigidBody*)pCollisionObject->getUserPointer();
            *ppHitRigidBody = pRB;
        }
        return XMVectorLerp( RayStart, RayEnd, Callback.m_closestHitFraction );
    }
    else
    {
        if( ppHitRigidBody != nullptr )
        {
            *ppHitRigidBody = nullptr;
        }
        return RayEnd;
    }
}

Vehicle::Vehicle( PhysicsWorld* pWorld, RigidBody* pChassis, const VehicleConfig* pConfig )
{
    m_pWorld = pWorld;
    m_DeathClock = 0;
    m_pChassisRigidBody = pChassis;
    m_Config = *pConfig;

    const UINT32 AxleCount = (UINT32)m_Config.AxleCount;

    if (AxleCount > 0)
    {
        btRaycastVehicle::btVehicleTuning VehicleTuning;
        m_pRaycaster = new btDefaultVehicleRaycaster(pWorld->GetInternalWorld());
        m_pVehicle = new btRaycastVehicle(VehicleTuning, m_pChassisRigidBody->GetInternalRigidBody(), m_pRaycaster);
        m_pChassisRigidBody->DisableDeactivation();

        int rightIndex = 0;
        int upIndex = 1;
        int forwardIndex = 2;
        m_pVehicle->setCoordinateSystem(rightIndex, upIndex, forwardIndex);
        for (UINT32 i = 0; i < AxleCount; ++i)
        {
            AddAxle(&m_Config.Axles[i], i == 0, i);
        }
        m_pWorld->GetInternalWorld()->addVehicle(m_pVehicle);
    }
    else
    {
        m_pVehicle = nullptr;
        m_pRaycaster = nullptr;
    }

    m_EngineForce = pConfig->EngineForce;
    m_BrakingForce = pConfig->BrakingForce;
}

Vehicle::~Vehicle()
{
    if (m_pVehicle != nullptr)
    { 
        m_pWorld->GetInternalWorld()->removeVehicle(m_pVehicle);
        delete m_pVehicle;
        m_pVehicle = nullptr;
        delete m_pRaycaster;
        m_pRaycaster = nullptr;
    }
    m_pChassisRigidBody = nullptr;
    m_pWorld = nullptr;
}

void Vehicle::AddAxle( const AxleConfig* pAxle, bool IsFrontAxle, UINT32 AxleIndex )
{
    btRaycastVehicle::btVehicleTuning VehicleTuning;
    btVector3 wheelDirectionCS0(0,-1,0);
    btVector3 wheelAxleCS(-1,0,0);

    btVector3 wheelPos;

    wheelPos.setX( pAxle->Width );
    wheelPos.setY( pAxle->YPos );
    wheelPos.setZ( pAxle->ZPos );

    INT StartWheelIndex = m_pVehicle->getNumWheels();

    m_pVehicle->addWheel( wheelPos, wheelDirectionCS0, wheelAxleCS, pAxle->SuspensionRestLength, pAxle->WheelRadius, VehicleTuning, IsFrontAxle );
    wheelPos.setX( -wheelPos.x() );
    m_pVehicle->addWheel( wheelPos, wheelDirectionCS0, wheelAxleCS, pAxle->SuspensionRestLength, pAxle->WheelRadius, VehicleTuning, IsFrontAxle );

    for (INT i = 0; i < 2; ++i)
    {
        btWheelInfo& wheel = m_pVehicle->getWheelInfo(i + StartWheelIndex);
        wheel.m_suspensionStiffness = pAxle->SuspensionStiffness;
        wheel.m_wheelsDampingRelaxation = pAxle->SuspensionDamping;
        wheel.m_wheelsDampingCompression = pAxle->SuspensionCompression;
        wheel.m_frictionSlip = pAxle->WheelFriction;
        wheel.m_rollInfluence = pAxle->RollInfluence;
    }
}

void Vehicle::SetGasAndBrake( FLOAT Gas, FLOAT Brake )
{
    for (UINT32 i = 0; i < m_Config.AxleCount; ++i)
    {
        const FLOAT TR = m_Config.Axles[i].ThrottleResponse;
        if (TR <= 0)
        {
            continue;
        }
        SetAxleGasAndBrake( i, TR * Gas, TR * Brake );
    }
}

void Vehicle::SetSteering( FLOAT Steering )
{
    Steering = std::max( std::min( 1.0f, Steering ), -1.0f );

    for (UINT32 i = 0; i < m_Config.AxleCount; ++i)
    {
        const FLOAT SR = m_Config.Axles[i].SteeringResponse;
        if (SR <= 0)
        {
            continue;
        }
        const FLOAT SMax = m_Config.Axles[i].SteeringMax;
        FLOAT AxleSteering = SR * Steering;
        if (AxleSteering > SMax)
        {
            AxleSteering = SMax;
        }
        else if (AxleSteering < -SMax)
        {
            AxleSteering = -SMax;
        }
        SetAxleSteering( i, AxleSteering );
    }
}

FLOAT Vehicle::GetSpeedKmHour() const
{
    return m_pVehicle->getCurrentSpeedKmHour();
}

void Vehicle::SetAxleGasAndBrake( UINT32 Axle, FLOAT Gas, FLOAT Brake )
{
    Gas *= m_EngineForce;
    Brake *= m_BrakingForce;
    INT WheelIndex = (INT)Axle * 2;
    m_pVehicle->applyEngineForce( Gas, WheelIndex );
    m_pVehicle->applyEngineForce( Gas, WheelIndex + 1 );
    m_pVehicle->setBrake( Brake, WheelIndex );
    m_pVehicle->setBrake( Brake, WheelIndex + 1 );
}

void Vehicle::SetAxleSteering( UINT32 Axle, FLOAT Steering )
{
    INT WheelIndex = (INT)Axle * 2;
    m_pVehicle->setSteeringValue( Steering, WheelIndex );
    m_pVehicle->setSteeringValue( Steering, WheelIndex + 1 );
}

UINT32 Vehicle::GetWheelCount() const
{
    if (m_pVehicle != nullptr)
    {
        return m_pVehicle->getNumWheels();
    }
    return 0;
}

XMMATRIX Vehicle::GetWheelTransform( UINT32 WheelIndex )
{
    m_pVehicle->updateWheelTransform( WheelIndex, true );

    XMFLOAT4X4 matWheel;
    m_pVehicle->getWheelInfo( WheelIndex ).m_worldTransform.getOpenGLMatrix( (btScalar*)&matWheel );
    return XMLoadFloat4x4( &matWheel );
}

void Vehicle::GetWheelTransform( UINT32 WheelIndex, XMFLOAT4* pOrientation, XMFLOAT3* pTranslation )
{
    m_pVehicle->updateWheelTransform( WheelIndex, true );

    const btTransform& WT = m_pVehicle->getWheelInfo( WheelIndex ).m_worldTransform;
    *pOrientation = (XMFLOAT4)WT.getRotation();
    *pTranslation = (XMFLOAT3)WT.getOrigin();
}

void Vehicle::EnableDeathClock( bool Enable )
{
    if (Enable)
    {
        m_DeathClock = 0.0f;
    }
    else
    {
        m_DeathClock = -1.0f;
    }
}

bool Vehicle::Tick( FLOAT DeltaTime, bool Occupied )
{
    if (m_DeathClock >= 0.0f)
    {
        const XMMATRIX matRB = m_pChassisRigidBody->GetWorldTransform();
        const XMVECTOR UpVector = matRB.r[1];
        const FLOAT Dot = XMVectorGetX( XMVector3Dot( UpVector, g_XMIdentityR1 ) );
        BOOL NotGood = ( Dot < 0.5f );
        BOOL ReallyNotGood = ( Dot < -0.25f );

        if (NotGood && !Occupied)
        {
            m_DeathClock += DeltaTime;
        }
        else if (ReallyNotGood)
        {
            m_DeathClock += DeltaTime;
        }
        else
        {
            m_DeathClock = 0.0f;
        }

        return m_DeathClock < 5.0f;
    }

    return true;
}

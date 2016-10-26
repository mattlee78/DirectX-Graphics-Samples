#pragma once

#include <unordered_set>
#include <vector>

#include <DirectXMath.h>
using namespace DirectX;

#include <DataFile.h>

class PhysicsWorld;

class btCollisionShape;
class btRigidBody;
class btMotionState;
class btTypedConstraint;
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btDynamicsWorld;
class btTriangleIndexVertexArray;
class btCollisionObject;
class btDefaultVehicleRaycaster;
class btRaycastVehicle;

struct AxleConfig
{
    FLOAT ZPos;
    FLOAT YPos;
    FLOAT Width;
    FLOAT WheelRadius;
    FLOAT SuspensionRestLength;
    FLOAT SuspensionStiffness;
    FLOAT SuspensionDamping;
    FLOAT SuspensionCompression;
    FLOAT WheelFriction;
    FLOAT RollInfluence;
    FLOAT SteeringMax;
    FLOAT ThrottleResponse;
    FLOAT SteeringResponse;
};
STRUCT_TEMPLATE_EXTERNAL(AxleConfig);

struct WaterThrusterConfig
{
    FLOAT Thrust;
    FLOAT SteeringMaxAngle;
    XMFLOAT3 LocalPosition;
};
STRUCT_TEMPLATE_EXTERNAL(WaterThrusterConfig);

struct SeatConfig
{
    XMFLOAT3 SeatPosition;
    XMFLOAT3 DismountPosition;
    UINT32 ActivityFlags;
    XMFLOAT3 ViewCenter;
    XMFLOAT2 ViewConstraintYaw;
    XMFLOAT2 ViewConstraintPitch;
};
STRUCT_TEMPLATE_EXTERNAL(SeatConfig);

struct VehicleConfig
{
    std::vector<const AxleConfig*> Axles;
    std::vector<const SeatConfig*> Seats;
    std::vector<const WaterThrusterConfig*> WaterThrusters;
    FLOAT EngineForce;
    FLOAT BrakingForce;
    FLOAT CollectiveMagnitude;
    FLOAT CyclicMagnitude;
    FLOAT RudderMagnitude;
};
STRUCT_TEMPLATE_EXTERNAL(VehicleConfig);

struct CollisionMeshDefinition
{
    const XMFLOAT3* pVertices;
    const UINT* pIndices;
    UINT VertexCount;
    UINT IndexCount;

    bool IsConvex() const { return IndexCount == 0; }
};

struct CollisionShapeDefinition
{
public:
    enum ShapeType
    {
        BoundingShape,
        BoundingBox,
        Sphere,
        Box,
        Plane,
        Capsule,
        ConvexHull,
        PolyMesh,
        CharacterController,
    };
    XMVECTOR vParams;
    XMVECTOR vOffset;
    ShapeType Type;
    FLOAT fMass;
    const CollisionMeshDefinition* pCollisionMesh;
};

class CollisionShape
{
protected:
    btCollisionShape* m_pShape;
    FLOAT m_SweptSphereRadius;
    CollisionShape( btCollisionShape* pShape );

public:
    virtual ~CollisionShape();
    VOID SetUniformScale( FLOAT Scale );

    static CollisionShape* CreateShape( const CollisionShapeDefinition* pDefinition );

    static CollisionShape* CreateBox( XMVECTOR vHalfSize );
    static CollisionShape* CreatePlane( XMVECTOR vPlane );
    static CollisionShape* CreateSphere( FLOAT Radius );
    static CollisionShape* CreateCapsule( FLOAT Radius, FLOAT TotalHeight );
    static CollisionShape* CreateConvexHull( const XMFLOAT3* pPositionArray, DWORD PositionCount, DWORD StrideBytes );
	static CollisionShape* CreateMesh( const XMFLOAT3* pPositionArray, DWORD PositionStrideBytes, DWORD VertexCount, const UINT* pTriangleListIndexArray, DWORD TriangleCount );

    btCollisionShape* GetInternalShape() const { return m_pShape; }
    FLOAT GetSweptSphereRadius() const { return m_SweptSphereRadius; }
};

class MeshCollisionShape : public CollisionShape
{
protected:
	btTriangleIndexVertexArray* m_pTriangleArray;
	XMFLOAT3* m_pVertexData;
	INT* m_pIndexData;

public:
	MeshCollisionShape( btCollisionShape* pShape, btTriangleIndexVertexArray* pTriangleArray, XMFLOAT3* pVertexData, INT* pIndexData )
		: CollisionShape( pShape ),
		  m_pTriangleArray( pTriangleArray ),
		  m_pVertexData( pVertexData ),
		  m_pIndexData( pIndexData )
	{
	}

	~MeshCollisionShape();
};

class CompoundCollisionShape : public CollisionShape
{
public:
    CompoundCollisionShape();

    VOID AddShape( CollisionShape* pShape, CXMMATRIX matLocalTransform );
};

class PhysicsObject
{
protected:
    PhysicsWorld* m_pWorld;
    VOID* m_pUserData;

public:
    VOID SetPhysicsWorld( PhysicsWorld* pWorld ) { m_pWorld = pWorld; }
    PhysicsWorld* GetPhysicsWorld() const { return m_pWorld; }

    VOID SetUserData( VOID* pUserData ) { m_pUserData = pUserData; }
    VOID* GetUserData() const { return m_pUserData; }
};

class RigidBody : public PhysicsObject
{
    friend class PhysicsWorld;

public:
    struct ContactPoint
    {
        XMFLOAT3 vLocalPosition;
        XMFLOAT3 vWorldDirection;
        RigidBody* pOtherBody;
    };
protected:
    btRigidBody* m_pRigidBody;
    btMotionState* m_pMotionState;
    ContactPoint* m_pContactPoints;
    DWORD m_dwTotalContactPoints;
    DWORD m_dwActiveContactPoints;
    CHAR m_strCollisionCallbackName[32];
    UINT32 m_CollisionMask;

public:
    RigidBody( CollisionShape* pShape, FLOAT Mass, CXMMATRIX matTransform );
    RigidBody( CollisionShape* pShape, FLOAT Mass, CXMMATRIX matTransform, CXMVECTOR vLocalInertia );
    ~RigidBody();

    XMMATRIX GetWorldTransform() const;
    XMVECTOR GetWorldPosition() const;

    XMVECTOR GetLinearVelocity() const;
    XMVECTOR GetAngularVelocity() const;
    VOID ApplyLinearImpulse( CXMVECTOR vLinearImpulse );
    VOID ApplyTorqueImpulse( CXMVECTOR vTorqueImpulse );
    VOID ApplyLocalTorqueImpulse( CXMVECTOR vTorqueImpulse );
    VOID ApplyLocalTorque( CXMVECTOR vTorque );
    VOID ApplyWorldTorque(CXMVECTOR vTorque);

    VOID LerpTo( CXMVECTOR vDesiredPos, CXMVECTOR vDesiredOrientation, FLOAT DeltaTime );
    VOID LerpTo( CXMMATRIX DesiredTransform, FLOAT DeltaTime );
    VOID SetWorldTransform( CXMMATRIX matTransform );

    VOID SetStatic();
    VOID SetDynamic();
    VOID SetKinematic();
    VOID EnableCollisions( bool Enable );
    VOID EnableKinematicCollisions();
    VOID DisableDeactivation();

    BOOL IsStatic() const;
    BOOL IsDynamic() const;

    BOOL IsActive() const;
    VOID Activate();
    VOID ClearForces();
    VOID ClearLinearVelocity();
    VOID ClearAngularVelocity();

    FLOAT GetInverseMass() const;

    VOID TrackContactPoints( DWORD dwCount );
    BOOL IsTrackingContactPoints() const { return m_pContactPoints != NULL; }
    DWORD GetNumContactPoints() const { return m_dwActiveContactPoints; }
    const ContactPoint& GetContactPoint( DWORD dwIndex ) const { return m_pContactPoints[dwIndex]; }
    VOID SetCollisionCallbackName( const CHAR* strCallback );
    BOOL IsCollisionCallbackEnabled() const { return m_strCollisionCallbackName[0] != '\0'; }

    VOID SetCollisionMask( UINT32 Mask ) { m_CollisionMask = Mask; }
    UINT32 GetCollisionMask() const { return m_CollisionMask; }
    BOOL TestCollisionMask( const RigidBody* pOtherRB ) const { return ( GetCollisionMask() & pOtherRB->GetCollisionMask() ) != 0; }

    XMVECTOR ShootRay( CXMVECTOR Ray, BOOL* pHit );

    btRigidBody* GetInternalRigidBody() const { return m_pRigidBody; }

    bool ApplyControls2D(FLOAT DeltaTime, FLOAT ForwardSpeed, FLOAT StrafeSpeed, FLOAT YawSpeed, FLOAT MaxSpeed, FLOAT MaxTurnSpeed);

protected:
    VOID Initialize( CollisionShape* pShape, FLOAT Mass, CXMMATRIX matTransform, CXMVECTOR vLocalInertia );
    VOID ClearContactPoints();
    BOOL AddContactPoint( CXMVECTOR vLocalPos, CXMVECTOR Distance, RigidBody* pOtherRigidBody );
};

class Constraint : public PhysicsObject
{
protected:
    RigidBody* m_pRigidBodyA;
    RigidBody* m_pRigidBodyB;
    btTypedConstraint* m_pConstraint;
    Constraint( RigidBody* pBodyA, RigidBody* pBodyB, btTypedConstraint* pConstraint );

public:
    ~Constraint();

    static Constraint* CreateBallSocket( RigidBody* pBodyA, RigidBody* pBodyB, FXMVECTOR vBallWorldPosition );
    static Constraint* CreateBallSocketMidpoint( RigidBody* pBodyA, RigidBody* pBodyB );

    btTypedConstraint* GetInternalConstraint() const { return m_pConstraint; }
};

class Vehicle
{
private:
    PhysicsWorld* m_pWorld;
    btRaycastVehicle* m_pVehicle;
    btDefaultVehicleRaycaster* m_pRaycaster;
    RigidBody* m_pChassisRigidBody;
    FLOAT m_EngineForce;
    FLOAT m_BrakingForce;
    FLOAT m_DeathClock;

    VehicleConfig m_Config;

public:
    Vehicle( PhysicsWorld* pWorld, RigidBody* pChassis, const VehicleConfig* pConfig );
    ~Vehicle();

    void SetGasAndBrake( FLOAT Gas, FLOAT Brake );
    void SetSteering( FLOAT Steering );
    FLOAT GetSpeedKmHour() const;
    FLOAT GetSpeedMSec() const { return GetSpeedKmHour() * 0.27777777f; }

    UINT32 GetWheelCount() const;
    XMMATRIX GetWheelTransform( UINT32 WheelIndex );
    void GetWheelTransform( UINT32 WheelIndex, XMFLOAT4* pOrientation, XMFLOAT3* pTranslation );

    void EnableDeathClock( bool Enable );
    bool Tick( FLOAT DeltaTime, bool Occupied );

private:
    void AddAxle( const AxleConfig* pAxle, bool IsFrontAxle, UINT32 AxleIndex );
    void SetAxleGasAndBrake( UINT32 Axle, FLOAT Gas, FLOAT Brake );
    void SetAxleSteering( UINT32 Axle, FLOAT Steering );
};

typedef std::unordered_set<PhysicsObject*> PhysicsObjectSet;
typedef std::unordered_set<CollisionShape*> CollisionShapeSet;
typedef std::vector<CollisionShape*> CollisionShapeVector;

class PhysicsWorld
{
protected:
    btDefaultCollisionConfiguration* m_pCollisionConfiguration;
    btCollisionDispatcher* m_pDispatcher;
    btBroadphaseInterface* m_pOverlappingPairCache;
    btSequentialImpulseConstraintSolver* m_pSolver;
    btDiscreteDynamicsWorld* m_pDynamicsWorld;

    PhysicsObjectSet m_OwnedObjects;

    CollisionShapeSet m_OwnedShapes;

    FLOAT m_Speed;

public:
    PhysicsWorld();
    ~PhysicsWorld();

    void Initialize(DWORD Flags, XMVECTOR vGravity);

    btDiscreteDynamicsWorld* GetInternalWorld() const { return m_pDynamicsWorld; }

    BOOL IsPaused() const { return m_Speed <= 0.0f; }
    VOID SetSpeed( FLOAT Speed ) { m_Speed = (Speed > 0.0f) ? Speed : 0.0f; }
    FLOAT GetSpeed() const { return m_Speed; }

    VOID Update( FLOAT DeltaTime );
    VOID DebugRender();

    VOID OwnShape( CollisionShape* pShape );
    VOID OwnObject( PhysicsObject* pObject );

    VOID AddRigidBody( RigidBody* pBody, BOOL WorldOwn = FALSE );
    VOID RemoveRigidBody( RigidBody* pBody );

    VOID AddConstraint( Constraint* pConstraint, BOOL WorldOwn = FALSE );
    VOID RemoveConstraint( Constraint* pConstraint );

    XMVECTOR ShootRay( RigidBody* pRigidBody, CXMVECTOR Ray, BOOL* pHit );
    XMVECTOR ShootRay( CXMVECTOR Origin, CXMVECTOR Ray, BOOL* pHit, RigidBody** ppHitRigidBody );

protected:
    static VOID SimulationTickCallback( btDynamicsWorld* pDynamicsWorld, FLOAT timeStep );
    VOID ClearContactPoints();
    VOID ReportContactPoints();
};

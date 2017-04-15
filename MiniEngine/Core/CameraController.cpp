//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "pch.h"
#include "CameraController.h"
#include "Camera.h"
#include "GameInput.h"

using namespace Math;
using namespace GameCore;

CameraController::CameraController( Camera& camera, Vector3 worldUp ) : m_TargetCamera( camera )
{
	m_WorldUp = Normalize(worldUp);
	m_WorldNorth = Normalize(Cross(m_WorldUp, Vector3(kXUnitVector)));
	m_WorldEast = Cross(m_WorldNorth, m_WorldUp);

	m_HorizontalLookSensitivity = 2.0f;
	m_VerticalLookSensitivity = 2.0f;
	m_MoveSpeed = 1000.0f;
	m_StrafeSpeed = 1000.0f;
	m_MouseSensitivityX = 1.0f;
	m_MouseSensitivityY = 1.0f;

	m_CurrentPitch = Sin(Dot(camera.GetForwardVec(), m_WorldUp));

	Vector3 forward = Normalize(Cross(m_WorldUp, camera.GetRightVec()));
	m_CurrentHeading = ATan2(-Dot(forward, m_WorldEast), Dot(forward, m_WorldNorth));

	m_FineMovement = false;
	m_FineRotation = false;
	m_Momentum = true;

	m_LastYaw = 0.0f;
	m_LastPitch = 0.0f;
	m_LastForward = 0.0f;
	m_LastStrafe = 0.0f;
	m_LastAscent = 0.0f;
}

namespace Graphics
{
	extern EnumVar DebugZoom;
}

void CameraController::Update( float deltaTime )
{
	(deltaTime);

	float timeScale = Graphics::DebugZoom == 0 ? 1.0f : Graphics::DebugZoom == 1 ? 0.5f : 0.25f;

	if (GameInput::IsFirstPressed(GameInput::kLThumbClick) || GameInput::IsFirstPressed(GameInput::kKey_lshift))
		m_FineMovement = !m_FineMovement;

	if (GameInput::IsFirstPressed(GameInput::kRThumbClick))
		m_FineRotation = !m_FineRotation;

	float speedScale = (m_FineMovement ? 0.1f : 1.0f) * timeScale;
	float panScale = (m_FineRotation ? 0.5f : 1.0f) * timeScale;

	float yaw = GameInput::GetTimeCorrectedAnalogInput( GameInput::kAnalogRightStickX ) * m_HorizontalLookSensitivity * panScale;
	float pitch = GameInput::GetTimeCorrectedAnalogInput( GameInput::kAnalogRightStickY ) * m_VerticalLookSensitivity * panScale;
	float forward =	m_MoveSpeed * speedScale * (
		GameInput::GetTimeCorrectedAnalogInput( GameInput::kAnalogLeftStickY ) +
		(GameInput::IsPressed( GameInput::kKey_w ) ? deltaTime : 0.0f) +
		(GameInput::IsPressed( GameInput::kKey_s ) ? -deltaTime : 0.0f)
		);
	float strafe = m_StrafeSpeed * speedScale * (
		GameInput::GetTimeCorrectedAnalogInput( GameInput::kAnalogLeftStickX  ) +
		(GameInput::IsPressed( GameInput::kKey_d ) ? deltaTime : 0.0f) +
		(GameInput::IsPressed( GameInput::kKey_a ) ? -deltaTime : 0.0f)
		);
	float ascent = m_StrafeSpeed * speedScale * (
		GameInput::GetTimeCorrectedAnalogInput( GameInput::kAnalogRightTrigger ) -
		GameInput::GetTimeCorrectedAnalogInput( GameInput::kAnalogLeftTrigger ) +
		(GameInput::IsPressed( GameInput::kKey_e ) ? deltaTime : 0.0f) +
		(GameInput::IsPressed( GameInput::kKey_q ) ? -deltaTime : 0.0f)
		);

	if (m_Momentum)
	{
		ApplyMomentum(m_LastYaw, yaw, deltaTime);
		ApplyMomentum(m_LastPitch, pitch, deltaTime);
		ApplyMomentum(m_LastForward, forward, deltaTime);
		ApplyMomentum(m_LastStrafe, strafe, deltaTime);
		ApplyMomentum(m_LastAscent, ascent, deltaTime);
	}

	// don't apply momentum to mouse inputs
	yaw += GameInput::GetAnalogInput(GameInput::kAnalogMouseX) * m_MouseSensitivityX;
	pitch += GameInput::GetAnalogInput(GameInput::kAnalogMouseY) * m_MouseSensitivityY;

	m_CurrentPitch += pitch;
	m_CurrentPitch = XMMin( XM_PIDIV2, m_CurrentPitch);
	m_CurrentPitch = XMMax(-XM_PIDIV2, m_CurrentPitch);

	m_CurrentHeading -= yaw;
	if (m_CurrentHeading > XM_PI)
		m_CurrentHeading -= XM_2PI;
	else if (m_CurrentHeading <= -XM_PI)
		m_CurrentHeading += XM_2PI; 

	Matrix3 orientation = Matrix3(m_WorldEast, m_WorldUp, -m_WorldNorth) * Matrix3::MakeYRotation( m_CurrentHeading ) * Matrix3::MakeXRotation( m_CurrentPitch );
	Vector3 position = orientation * Vector3( strafe, ascent, -forward ) + m_TargetCamera.GetPosition();
	m_TargetCamera.SetTransform( AffineTransform( orientation, position ) );
	m_TargetCamera.Update();
}

void CameraController::ApplyMomentum( float& oldValue, float& newValue, float deltaTime )
{
	float blendedValue;
	if (Abs(newValue) > Abs(oldValue))
		blendedValue = Lerp(newValue, oldValue, Pow(0.6f, deltaTime * 60.0f));
	else
		blendedValue = Lerp(newValue, oldValue, Pow(0.8f, deltaTime * 60.0f));
	oldValue = blendedValue;
	newValue = blendedValue;
}

FollowCameraController::FollowCameraController(Camera& TargetCamera, const Vector3& CameraOffset, float XZRadius, float YDistance, float TargetYOffset)
    : m_TargetCamera(TargetCamera)
{
    m_CameraOffsetModelSpace = CameraOffset;
    m_MaxXZDistance = XZRadius;
    m_MaxYDistance = YDistance;
    m_TargetYOffset = TargetYOffset;
    m_CameraMoveSpeed = 5.0f;
    m_TargetMoveSpeed = 10.0f;
    m_LastTargetPos = Vector3(0, 0, 0);
    m_pLastTargetObject = nullptr;
}

void FollowCameraController::Update(Matrix4& TargetWorldTransform, float DeltaTime, void* pTargetObject)
{
    const bool ResetCamera = (m_pLastTargetObject != pTargetObject);
    m_pLastTargetObject = pTargetObject;

    Vector3 CurrentCameraPos = m_TargetCamera.GetPosition();
    XMVECTOR Det;
    XMMATRIX InvTargetWorld = XMMatrixInverse(&Det, TargetWorldTransform);
    XMVECTOR CameraPosLocal = XMVector3TransformCoord(CurrentCameraPos, InvTargetWorld);
    XMVECTOR CameraOffset = CameraPosLocal - m_CameraOffsetModelSpace;
    XMVECTOR CameraOffsetXZ = XMVectorSwizzle<0, 2, 3, 3>(CameraOffset);
    float CameraOffsetY = XMVectorGetY(CameraOffset);

    XMVECTOR TargetCameraPosLocal = CameraPosLocal;
    XMVECTOR DistXZSquared = XMVector2LengthSq(CameraOffsetXZ);
    if (XMVectorGetX(DistXZSquared) > (m_MaxXZDistance * m_MaxXZDistance))
    {
        XMVECTOR NormOffsetXZ = XMVector2Normalize(CameraOffsetXZ);
        XMVECTOR OffsetXZ = NormOffsetXZ * m_MaxXZDistance;
        TargetCameraPosLocal = XMVectorSelect(TargetCameraPosLocal, XMVectorSwizzle<0, 0, 1, 1>(OffsetXZ), g_XMSelect1010);
        TargetCameraPosLocal += m_CameraOffsetModelSpace;
    }
    
    if (CameraOffsetY > m_MaxYDistance)
    {
        CameraOffsetY = m_MaxYDistance;
    }
    else if (CameraOffsetY < -m_MaxYDistance)
    {
        CameraOffsetY = -m_MaxYDistance;
    }
    TargetCameraPosLocal = XMVectorSetY(TargetCameraPosLocal, CameraOffsetY + m_CameraOffsetModelSpace.GetY());

    FLOAT LerpValue = std::min(1.0f, DeltaTime * m_CameraMoveSpeed);
    if (ResetCamera)
    {
        LerpValue = 1.0f;
    }
    XMVECTOR NewCameraPosLocal = XMVectorLerp(CameraPosLocal, TargetCameraPosLocal, LerpValue);
    XMVECTOR NewCameraPos = XMVector3TransformCoord(NewCameraPosLocal, TargetWorldTransform);

    Vector3 NewEyePos = Vector3(TargetWorldTransform.GetW());
    NewEyePos += Vector3(TargetWorldTransform.GetY() * m_TargetYOffset);

    LerpValue = std::min(1.0f, DeltaTime * m_TargetMoveSpeed);
    if (ResetCamera)
    {
        LerpValue = 1.0f;
    }
    NewEyePos = Vector3(XMVectorLerp(m_LastTargetPos, NewEyePos, LerpValue));
    m_LastTargetPos = NewEyePos;

    m_TargetCamera.SetEyeAtUp(Vector3(NewCameraPos), NewEyePos, Vector3(kYUnitVector));
    m_TargetCamera.Update();
}

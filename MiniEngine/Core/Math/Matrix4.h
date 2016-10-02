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

#pragma once

#include "Transform.h"

namespace Math
{
	__declspec(align(64)) class Matrix4
	{
	public:
		INLINE Matrix4() {}
		INLINE Matrix4( Vector3 x, Vector3 y, Vector3 z, Vector3 w )
		{
			m_mat.r[0] = SetWToZero(x); m_mat.r[1] = SetWToZero(y);
			m_mat.r[2] = SetWToZero(z); m_mat.r[3] = SetWToOne(w);
		}
		INLINE Matrix4( Vector4 x, Vector4 y, Vector4 z, Vector4 w ) { m_mat.r[0] = x; m_mat.r[1] = y; m_mat.r[2] = z; m_mat.r[3] = w; }
		INLINE Matrix4( const Matrix4& mat ) { m_mat = mat.m_mat; }
		INLINE Matrix4( const Matrix3& mat )
		{
			m_mat.r[0] = SetWToZero(mat.GetX());
			m_mat.r[1] = SetWToZero(mat.GetY());
			m_mat.r[2] = SetWToZero(mat.GetZ());
			m_mat.r[3] = CreateWUnitVector();
		}
		INLINE Matrix4( const Matrix3& xyz, Vector3 w )
		{
			m_mat.r[0] = SetWToZero(xyz.GetX());
			m_mat.r[1] = SetWToZero(xyz.GetY());
			m_mat.r[2] = SetWToZero(xyz.GetZ());
			m_mat.r[3] = SetWToOne(w);
		}
		INLINE Matrix4( const AffineTransform& xform ) { *this = Matrix4( xform.GetBasis(), xform.GetTranslation()); }
		INLINE Matrix4( const OrthogonalTransform& xform ) { *this = Matrix4( Matrix3(xform.GetRotation()), xform.GetTranslation() ); }
		INLINE explicit Matrix4( const XMMATRIX& mat ) { m_mat = mat; }
		INLINE explicit Matrix4( EIdentityTag ) { m_mat = XMMatrixIdentity(); }
		INLINE explicit Matrix4( EZeroTag ) { m_mat.r[0] = m_mat.r[1] = m_mat.r[2] = m_mat.r[3] = SplatZero(); }

		INLINE const Matrix3& Get3x3() const { return (const Matrix3&)*this; }

		INLINE Vector4 GetX() const { return Vector4(m_mat.r[0]); }
		INLINE Vector4 GetY() const { return Vector4(m_mat.r[1]); }
		INLINE Vector4 GetZ() const { return Vector4(m_mat.r[2]); }
		INLINE Vector4 GetW() const { return Vector4(m_mat.r[3]); }

		INLINE void SetX(Vector4 x) { m_mat.r[0] = x; }
		INLINE void SetY(Vector4 y) { m_mat.r[1] = y; }
		INLINE void SetZ(Vector4 z) { m_mat.r[2] = z; }
		INLINE void SetW(Vector4 w) { m_mat.r[3] = w; }

		INLINE operator XMMATRIX() const { return m_mat; }

		INLINE Vector4 operator* ( Vector3 vec ) const { return Vector4(XMVector3Transform(vec, m_mat)); }
		INLINE Vector4 operator* ( Vector4 vec ) const { return Vector4(XMVector4Transform(vec, m_mat)); }
		INLINE Matrix4 operator* ( const Matrix4& mat ) const { return Matrix4(XMMatrixMultiply(mat, m_mat)); }

		static INLINE Matrix4 MakeScale( float scale ) { return Matrix4(XMMatrixScaling(scale, scale, scale)); }
		static INLINE Matrix4 MakeScale( Vector3 scale ) { return Matrix4(XMMatrixScalingFromVector(scale)); }

        void Decompose(Vector3& Position, float& Scale, Vector4& Orientation) const
        {
            Position = Vector3(m_mat.r[3]);
            static const XMVECTOR vOneThird = { 0.33333333f, 0, 0, 0 };
            XMVECTOR vScaleX = XMVector3LengthEst(m_mat.r[0]);
            XMVECTOR vScaleY = XMVector3LengthEst(m_mat.r[1]);
            XMVECTOR vScaleZ = XMVector3LengthEst(m_mat.r[2]);
            XMVECTOR vScaleBlend = XMVectorMultiplyAdd(vScaleX, vOneThird, XMVectorMultiplyAdd(vScaleY, vOneThird, XMVectorMultiply(vScaleZ, vOneThird)));
            Scale = XMVectorGetX(vScaleBlend);

            XMMATRIX matRotation = m_mat;
            matRotation.r[3] = XMQuaternionIdentity();
            Orientation = Vector4(XMQuaternionRotationMatrix(matRotation));
        }

        void Compose(const Vector3& Position, float Scale, const Vector4& Orientation)
        {
            const XMVECTOR vS = XMVectorReplicate(Scale);
            XMMATRIX m = XMMatrixRotationQuaternion(Orientation);
            m.r[0] *= vS;
            m.r[1] *= vS;
            m.r[2] *= vS;
            m.r[3] = XMVectorSelect(m.r[3], Position, g_XMSelect1110);
            m_mat = m;
        }

        void Compose(const Vector4& PositionScale, const Vector4& Orientation)
        {
            const XMVECTOR vS = XMVectorSplatW(PositionScale);
            XMMATRIX m = XMMatrixRotationQuaternion(Orientation);
            m.r[0] *= vS;
            m.r[1] *= vS;
            m.r[2] *= vS;
            m.r[3] = XMVectorSelect(m.r[3], PositionScale, g_XMSelect1110);
            m_mat = m;
        }

    private:
		XMMATRIX m_mat;
	};
}

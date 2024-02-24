//========= Copyright Valve Corporation, All rights reserved. ============//
#pragma once
#include <cmath>

#define  FORCEINLINE			__forceinline

#define DECL_ALIGN(x) __declspec(align(x))
#define ALIGN16 DECL_ALIGN(16)
#define ALIGN16_POST DECL_ALIGN(16)

#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#define M_PI_F		((float)(M_PI))	// Shouldn't collide with anything.
#define DEG2RAD( x  )  ( (float)(x) * (float)(M_PI_F / 180.f) )
#define RAD2DEG( x  )  ( (float)(x) * (float)(180.f / M_PI_F) )

typedef float vec_t;


class Vector2D
{
public:
	// Members
	vec_t x, y;

	// Construction/destruction
	Vector2D(void);
	Vector2D(vec_t X, vec_t Y);
	Vector2D(const float *pFloat);

	// Initialization
	void Init(vec_t ix = 0.0f, vec_t iy = 0.0f);

	// Got any nasty NAN's?
	bool IsValid() const;

	// array access...
	vec_t operator[](int i) const;
	vec_t &operator[](int i);

	// Base address...
	vec_t *Base();
	vec_t const *Base() const;

	// Initialization methods
	void Random(float minVal, float maxVal);

	// equality
	bool operator==(const Vector2D &v) const;
	bool operator!=(const Vector2D &v) const;

	// arithmetic operations
	Vector2D &operator+=(const Vector2D &v);
	Vector2D &operator-=(const Vector2D &v);
	Vector2D &operator*=(const Vector2D &v);
	Vector2D &operator*=(float s);
	Vector2D &operator/=(const Vector2D &v);
	Vector2D &operator/=(float s);

	// negate the Vector2D components
	void	Negate();

	// Get the Vector2D's magnitude.
	vec_t	Length() const;

	// Get the Vector2D's magnitude squared.
	vec_t	LengthSqr(void) const;

	// return true if this vector is (0,0) within tolerance
	bool IsZero(float tolerance = 0.01f) const
	{
		return (x > -tolerance && x < tolerance &&
			y > -tolerance && y < tolerance);
	}

	// Normalize in place and return the old length.
	vec_t	NormalizeInPlace();

	// Compare length.
	bool	IsLengthGreaterThan(float val) const;
	bool	IsLengthLessThan(float val) const;

	// Get the distance from this Vector2D to the other one.
	vec_t	DistTo(const Vector2D &vOther) const;

	// Get the distance from this Vector2D to the other one squared.
	vec_t	DistToSqr(const Vector2D &vOther) const;

	// Copy
	void	CopyToArray(float *rgfl) const;

	// Multiply, add, and assign to this (ie: *this = a + b * scalar). This
	// is about 12% faster than the actual Vector2D equation (because it's done per-component
	// rather than per-Vector2D).
	void	MulAdd(const Vector2D &a, const Vector2D &b, float scalar);

	// Dot product.
	vec_t	Dot(const Vector2D &vOther) const;

	// assignment
	Vector2D &operator=(const Vector2D &vOther);

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// copy constructors
	Vector2D(const Vector2D &vOther);

	// arithmetic operations
	Vector2D	operator-(void) const;

	Vector2D	operator+(const Vector2D &v) const;
	Vector2D	operator-(const Vector2D &v) const;
	Vector2D	operator*(const Vector2D &v) const;
	Vector2D	operator/(const Vector2D &v) const;
	Vector2D	operator*(float fl) const;
	Vector2D	operator/(float fl) const;

	// Cross product between two vectors.
	Vector2D	Cross(const Vector2D &vOther) const;

	// Returns a Vector2D with the min or max in X, Y, and Z.
	Vector2D	Min(const Vector2D &vOther) const;
	Vector2D	Max(const Vector2D &vOther) const;

#else

private:
	// No copy constructors allowed if we're in optimal mode
	Vector2D(const Vector2D &vOther);
#endif
};

class VectorByValue;

class Vector
{
public:
	// Members
	vec_t x, y, z;

	// Construction/destruction:
	Vector(void);
	Vector(vec_t X, vec_t Y, vec_t Z);
	explicit Vector(vec_t XYZ); ///< broadcast initialize

	// Initialization
	void Init(vec_t ix = 0.0f, vec_t iy = 0.0f, vec_t iz = 0.0f);
	// TODO (Ilya): Should there be an init that takes a single float for consistency?

   // Got any nasty NAN's?
	bool IsValid() const;
	void Invalidate();

	// array access...
	vec_t operator[](int i) const;
	vec_t &operator[](int i);

	// Base address...
	vec_t *Base();
	vec_t const *Base() const;

	// Cast to Vector2D...
	Vector2D &AsVector2D();
	const Vector2D &AsVector2D() const;

	// Initialization methods
	void Random(vec_t minVal, vec_t maxVal);
	inline void Zero(); ///< zero out a vector

	// equality
	bool operator==(const Vector &v) const;
	bool operator!=(const Vector &v) const;

	// arithmetic operations
	FORCEINLINE Vector &operator+=(const Vector &v);
	FORCEINLINE Vector &operator-=(const Vector &v);
	FORCEINLINE Vector &operator*=(const Vector &v);
	FORCEINLINE Vector &operator*=(float s);
	FORCEINLINE Vector &operator/=(const Vector &v);
	FORCEINLINE Vector &operator/=(float s);
	FORCEINLINE Vector &operator+=(float fl); ///< broadcast add
	FORCEINLINE Vector &operator-=(float fl); ///< broadcast sub			

// negate the vector components
	void	Negate();

	// Get the vector's magnitude.
	inline vec_t	Length() const;

	// Get the vector's magnitude squared.
	FORCEINLINE vec_t LengthSqr(void) const
	{
		return (x * x + y * y + z * z);
	}

	

	// return true if this vector is (0,0,0) within tolerance
	bool IsZero(float tolerance = 0.01f) const
	{
		return (x > -tolerance && x < tolerance &&
			y > -tolerance && y < tolerance &&
			z > -tolerance && z < tolerance);
	}


	vec_t	NormalizeInPlace();
	Vector	Normalized() const;
	bool	IsLengthGreaterThan(float val) const;
	bool	IsLengthLessThan(float val) const;

	// check if a vector is within the box defined by two other vectors
	FORCEINLINE bool WithinAABox(Vector const &boxmin, Vector const &boxmax);

	// Get the distance from this vector to the other one.
	vec_t	DistTo(const Vector &vOther) const;

	// Get the distance from this vector to the other one squared.
	// NJS: note, VC wasn't inlining it correctly in several deeply nested inlines due to being an 'out of line' inline.  
	// may be able to tidy this up after switching to VC7
	FORCEINLINE vec_t DistToSqr(const Vector &vOther) const
	{
		Vector delta;

		delta.x = x - vOther.x;
		delta.y = y - vOther.y;
		delta.z = z - vOther.z;

		return delta.LengthSqr();
	}

	// Copy
	void	CopyToArray(float *rgfl) const;

	// Multiply, add, and assign to this (ie: *this = a + b * scalar). This
	// is about 12% faster than the actual vector equation (because it's done per-component
	// rather than per-vector).
	void	MulAdd(const Vector &a, const Vector &b, float scalar);

	// Dot product.
	vec_t	Dot(const Vector &vOther) const;

	// assignment
	Vector &operator=(const Vector &vOther);

	// 2d
	vec_t	Length2D(void) const;
	vec_t	Length2DSqr(void) const;

	operator VectorByValue &() { return *((VectorByValue *)(this)); }
	operator const VectorByValue &() const { return *((const VectorByValue *)(this)); }

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// copy constructors
//	Vector(const Vector &vOther);

	// arithmetic operations
	Vector	operator-(void) const;

	Vector	operator+(const Vector &v) const;
	Vector	operator-(const Vector &v) const;
	Vector	operator*(const Vector &v) const;
	Vector	operator/(const Vector &v) const;
	Vector	operator*(float fl) const;
	Vector	operator/(float fl) const;

	// Cross product between two vectors.
	Vector	Cross(const Vector &vOther) const;

	// Returns a vector with the min or max in X, Y, and Z.
	Vector	Min(const Vector &vOther) const;
	Vector	Max(const Vector &vOther) const;

#else

private:
	// No copy constructors allowed if we're in optimal mode
	Vector(const Vector &vOther);
#endif
};

inline void Vector::Init(vec_t ix, vec_t iy, vec_t iz)
{
	x = ix; y = iy; z = iz;
}


inline Vector::Vector(void)
{
#ifdef _DEBUG
#ifdef VECTOR_PARANOIA
	// Initialize to NAN to catch errors
	x = y = z = VEC_T_NAN;
#endif
#endif
}

FORCEINLINE Vector::Vector(vec_t X, vec_t Y, vec_t Z)
{
	x = X; y = Y; z = Z;
}

inline vec_t &Vector::operator[](int i)
{
	return ((vec_t *)this)[i];
}

inline vec_t Vector::operator[](int i) const
{
	return ((vec_t *)this)[i];
}

class VectorByValue : public Vector
{
public:
	// Construction/destruction:
	VectorByValue(void) : Vector() {}
	VectorByValue(vec_t X, vec_t Y, vec_t Z) : Vector(X, Y, Z) {}
	VectorByValue(const VectorByValue &vOther) { *this = vOther; }
};

class ALIGN16 VectorAligned : public Vector
{
public:
	inline VectorAligned(void) {};
	inline VectorAligned(vec_t X, vec_t Y, vec_t Z)
	{
		Init(X, Y, Z);
	}

#ifdef VECTOR_NO_SLOW_OPERATIONS

private:
	// No copy constructors allowed if we're in optimal mode
	VectorAligned(const VectorAligned &vOther);
	VectorAligned(const Vector &vOther);

#else
public:
	explicit VectorAligned(const Vector &vOther)
	{
		Init(vOther.x, vOther.y, vOther.z);
	}

	VectorAligned &operator=(const Vector &vOther)
	{
		Init(vOther.x, vOther.y, vOther.z);
		return *this;
	}

#endif
	float w;	// this space is used anyway
} ALIGN16_POST;


FORCEINLINE void VectorAdd(const Vector &a, const Vector &b, Vector &c)
{
	c.x = a.x + b.x;
	c.y = a.y + b.y;
	c.z = a.z + b.z;
}

FORCEINLINE void VectorSubtract(const Vector &a, const Vector &b, Vector &c)
{
	c.x = a.x - b.x;
	c.y = a.y - b.y;
	c.z = a.z - b.z;
}

inline unsigned long &FloatBits(vec_t &f)
{
	return *reinterpret_cast<unsigned long *>(&f);
}

inline bool IsFinite(vec_t f)
{
	return ((FloatBits(f) & 0x7F800000) != 0x7F800000);
}

FORCEINLINE void VectorMultiply(const Vector &a, vec_t b, Vector &c)
{
	c.x = a.x * b;
	c.y = a.y * b;
	c.z = a.z * b;
}

FORCEINLINE void VectorMultiply(const Vector &a, const Vector &b, Vector &c)
{
	c.x = a.x * b.x;
	c.y = a.y * b.y;
	c.z = a.z * b.z;
}

inline void VectorClear(Vector &a)
{
	a.x = a.y = a.z = 0.0f;
}

FORCEINLINE void VectorCopy(const Vector &src, Vector &dst)
{
	dst.x = src.x;
	dst.y = src.y;
	dst.z = src.z;
}

typedef Vector vec3_t;

struct matrix3x4_t
{
	matrix3x4_t() = default;
	matrix3x4_t(
		float m00, float m01, float m02, float m03,
		float m10, float m11, float m12, float m13,
		float m20, float m21, float m22, float m23)
	{
		m_matrix[0][0] = m00;
		m_matrix[0][1] = m01;
		m_matrix[0][2] = m02;
		m_matrix[0][3] = m03;
		m_matrix[1][0] = m10;
		m_matrix[1][1] = m11;
		m_matrix[1][2] = m12;
		m_matrix[1][3] = m13;
		m_matrix[2][0] = m20;
		m_matrix[2][1] = m21;
		m_matrix[2][2] = m22;
		m_matrix[2][3] = m23;
	}
	matrix3x4_t(const vec3_t& x_axis, const vec3_t& y_axis, const vec3_t& z_axis, const vec3_t& vec_origin)
	{
		init(x_axis, y_axis, z_axis, vec_origin);
	}

	void init(const vec3_t& x_axis, const vec3_t& y_axis, const vec3_t& z_axis, const vec3_t& vec_origin)
	{
		m_matrix[0][0] = x_axis.x;
		m_matrix[0][1] = y_axis.x;
		m_matrix[0][2] = z_axis.x;
		m_matrix[0][3] = vec_origin.x;
		m_matrix[1][0] = x_axis.y;
		m_matrix[1][1] = y_axis.y;
		m_matrix[1][2] = z_axis.y;
		m_matrix[1][3] = vec_origin.y;
		m_matrix[2][0] = x_axis.z;
		m_matrix[2][1] = y_axis.z;
		m_matrix[2][2] = z_axis.z;
		m_matrix[2][3] = vec_origin.z;
	}

	float* operator[](int i)
	{
		return m_matrix[i];
	}

	const float* operator[](int i) const
	{
		return m_matrix[i];
	}

	float m_matrix[3][4];

	float* Base() { return &m_matrix[0][0]; }
	const float* Base() const { return &m_matrix[0][0]; }

};

class Vector4D;
class VPlane;
class QAngle;

class VMatrix
{
public:

	VMatrix()
	{
	}

	VMatrix(
		vec_t m00, vec_t m01, vec_t m02, vec_t m03,
		vec_t m10, vec_t m11, vec_t m12, vec_t m13,
		vec_t m20, vec_t m21, vec_t m22, vec_t m23,
		vec_t m30, vec_t m31, vec_t m32, vec_t m33
	)
	{
		Init(
			m00, m01, m02, m03,
			m10, m11, m12, m13,
			m20, m21, m22, m23,
			m30, m31, m32, m33
		);
	}

	// Creates a matrix where the X axis = forward
	// the Y axis = left, and the Z axis = up
	VMatrix(const Vector &forward, const Vector &left, const Vector &up);
	VMatrix(const Vector &forward, const Vector &left, const Vector &up, const Vector &translation);

	// Construct from a 3x4 matrix
	VMatrix(const matrix3x4_t &matrix3x4);

	// Set the values in the matrix.
	void		Init(
		vec_t m00, vec_t m01, vec_t m02, vec_t m03,
		vec_t m10, vec_t m11, vec_t m12, vec_t m13,
		vec_t m20, vec_t m21, vec_t m22, vec_t m23,
		vec_t m30, vec_t m31, vec_t m32, vec_t m33
	)
	{
		m[0][0] = m00;
		m[0][1] = m01;
		m[0][2] = m02;
		m[0][3] = m03;

		m[1][0] = m10;
		m[1][1] = m11;
		m[1][2] = m12;
		m[1][3] = m13;

		m[2][0] = m20;
		m[2][1] = m21;
		m[2][2] = m22;
		m[2][3] = m23;

		m[3][0] = m30;
		m[3][1] = m31;
		m[3][2] = m32;
		m[3][3] = m33;
	}


	// Initialize from a 3x4
	void		Init(const matrix3x4_t &matrix3x4);

	// array access
	inline float *operator[](int i)
	{
		return m[i];
	}

	inline const float *operator[](int i) const
	{
		return m[i];
	}

	// Get a pointer to m[0][0]
	inline float *Base()
	{
		return &m[0][0];
	}

	inline const float *Base() const
	{
		return &m[0][0];
	}

	void		SetLeft(const Vector &vLeft);
	void		SetUp(const Vector &vUp);
	void		SetForward(const Vector &vForward);

	void		GetBasisVectors(Vector &vForward, Vector &vLeft, Vector &vUp) const;
	void		SetBasisVectors(const Vector &vForward, const Vector &vLeft, const Vector &vUp);

	// Get/set the translation.
	Vector &GetTranslation(Vector &vTrans) const;
	void		SetTranslation(const Vector &vTrans);

	void		PreTranslate(const Vector &vTrans);
	void		PostTranslate(const Vector &vTrans);

	const matrix3x4_t &As3x4() const;
	void		CopyFrom3x4(const matrix3x4_t &m3x4);
	void		Set3x4(matrix3x4_t &matrix3x4) const;

	bool		operator==(const VMatrix &src) const;
	bool		operator!=(const VMatrix &src) const { return !(*this == src); }

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// Access the basis vectors.
	Vector		GetLeft() const;
	Vector		GetUp() const;
	Vector		GetForward() const;
	Vector		GetTranslation() const;
#endif


	// Matrix->vector operations.
public:
	// Multiply by a 3D vector (same as operator*).
	void		V3Mul(const Vector &vIn, Vector &vOut) const;

	// Multiply by a 4D vector.
	void		V4Mul(const Vector4D &vIn, Vector4D &vOut) const;

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// Applies the rotation (ignores translation in the matrix). (This just calls VMul3x3).
	Vector		ApplyRotation(const Vector &vVec) const;

	// Multiply by a vector (divides by w, assumes input w is 1).
	Vector		operator*(const Vector &vVec) const;

	// Multiply by the upper 3x3 part of the matrix (ie: only apply rotation).
	Vector		VMul3x3(const Vector &vVec) const;

	// Apply the inverse (transposed) rotation (only works on pure rotation matrix)
	Vector		VMul3x3Transpose(const Vector &vVec) const;

	// Multiply by the upper 3 rows.
	Vector		VMul4x3(const Vector &vVec) const;

	// Apply the inverse (transposed) transformation (only works on pure rotation/translation)
	Vector		VMul4x3Transpose(const Vector &vVec) const;
#endif


	// Matrix->plane operations.
public:
	// Transform the plane. The matrix can only contain translation and rotation.
	void		TransformPlane(const VPlane &inPlane, VPlane &outPlane) const;

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// Just calls TransformPlane and returns the result.
	VPlane		operator*(const VPlane &thePlane) const;
#endif

	// Matrix->matrix operations.
public:

	VMatrix &operator=(const VMatrix &mOther);

	// Multiply two matrices (out = this * vm).
	void		MatrixMul(const VMatrix &vm, VMatrix &out) const
	{
		out.Init(
			m[0][0]*vm.m[0][0] + m[0][1]*vm.m[1][0] + m[0][2]*vm.m[2][0] + m[0][3]*vm.m[3][0],
			m[0][0]*vm.m[0][1] + m[0][1]*vm.m[1][1] + m[0][2]*vm.m[2][1] + m[0][3]*vm.m[3][1],
			m[0][0]*vm.m[0][2] + m[0][1]*vm.m[1][2] + m[0][2]*vm.m[2][2] + m[0][3]*vm.m[3][2],
			m[0][0]*vm.m[0][3] + m[0][1]*vm.m[1][3] + m[0][2]*vm.m[2][3] + m[0][3]*vm.m[3][3],

			m[1][0]*vm.m[0][0] + m[1][1]*vm.m[1][0] + m[1][2]*vm.m[2][0] + m[1][3]*vm.m[3][0],
			m[1][0]*vm.m[0][1] + m[1][1]*vm.m[1][1] + m[1][2]*vm.m[2][1] + m[1][3]*vm.m[3][1],
			m[1][0]*vm.m[0][2] + m[1][1]*vm.m[1][2] + m[1][2]*vm.m[2][2] + m[1][3]*vm.m[3][2],
			m[1][0]*vm.m[0][3] + m[1][1]*vm.m[1][3] + m[1][2]*vm.m[2][3] + m[1][3]*vm.m[3][3],

			m[2][0]*vm.m[0][0] + m[2][1]*vm.m[1][0] + m[2][2]*vm.m[2][0] + m[2][3]*vm.m[3][0],
			m[2][0]*vm.m[0][1] + m[2][1]*vm.m[1][1] + m[2][2]*vm.m[2][1] + m[2][3]*vm.m[3][1],
			m[2][0]*vm.m[0][2] + m[2][1]*vm.m[1][2] + m[2][2]*vm.m[2][2] + m[2][3]*vm.m[3][2],
			m[2][0]*vm.m[0][3] + m[2][1]*vm.m[1][3] + m[2][2]*vm.m[2][3] + m[2][3]*vm.m[3][3],

			m[3][0]*vm.m[0][0] + m[3][1]*vm.m[1][0] + m[3][2]*vm.m[2][0] + m[3][3]*vm.m[3][0],
			m[3][0]*vm.m[0][1] + m[3][1]*vm.m[1][1] + m[3][2]*vm.m[2][1] + m[3][3]*vm.m[3][1],
			m[3][0]*vm.m[0][2] + m[3][1]*vm.m[1][2] + m[3][2]*vm.m[2][2] + m[3][3]*vm.m[3][2],
			m[3][0]*vm.m[0][3] + m[3][1]*vm.m[1][3] + m[3][2]*vm.m[2][3] + m[3][3]*vm.m[3][3]
		);
	}

	// Add two matrices.
	const VMatrix &operator+=(const VMatrix &other);

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// Just calls MatrixMul and returns the result.	
	VMatrix		operator*(const VMatrix &mOther) const;

	// Add/Subtract two matrices.
	VMatrix		operator+(const VMatrix &other) const;
	VMatrix		operator-(const VMatrix &other) const;

	// Negation.
	VMatrix		operator-() const;

	// Return inverse matrix. Be careful because the results are undefined 
	// if the matrix doesn't have an inverse (ie: InverseGeneral returns false).
	VMatrix		operator~() const;
#endif

	// Matrix operations.
public:
	// Set to identity.
	void		Identity();

	bool		IsIdentity() const;

	// Setup a matrix for origin and angles.
	void		SetupMatrixOrgAngles(const Vector &origin, const QAngle &vAngles);

	// Setup a matrix for angles and no translation.
	void		SetupMatrixAngles(const QAngle &vAngles);

	// General inverse. This may fail so check the return!
	bool		InverseGeneral(VMatrix &vInverse) const;

	// Does a fast inverse, assuming the matrix only contains translation and rotation.
	void		InverseTR(VMatrix &mRet) const;

	// Usually used for debug checks. Returns true if the upper 3x3 contains
	// unit vectors and they are all orthogonal.
	bool		IsRotationMatrix() const;

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// This calls the other InverseTR and returns the result.
	VMatrix		InverseTR() const;

	// Get the scale of the matrix's basis vectors.
	Vector		GetScale() const;

	// (Fast) multiply by a scaling matrix setup from vScale.
	VMatrix		Scale(const Vector &vScale);

	// Normalize the basis vectors.
	VMatrix		NormalizeBasisVectors() const;

	// Transpose.
	VMatrix		Transpose() const;

	// Transpose upper-left 3x3.
	VMatrix		Transpose3x3() const;
#endif

public:
	// The matrix.
	vec_t		m[4][4];
};

inline const matrix3x4_t& VMatrix::As3x4() const
{
	return *((const matrix3x4_t*)this);
}

/*inline matrix3x4_t& VMatrix::As3x4()
{
	return *((matrix3x4_t*)this);
}*/


inline Vector VMatrix::operator*(const Vector& vVec) const
{
	Vector vRet;
	vRet.x = m[0][0] * vVec.x + m[0][1] * vVec.y + m[0][2] * vVec.z + m[0][3];
	vRet.y = m[1][0] * vVec.x + m[1][1] * vVec.y + m[1][2] * vVec.z + m[1][3];
	vRet.z = m[2][0] * vVec.x + m[2][1] * vVec.y + m[2][2] * vVec.z + m[2][3];

	return vRet;
}

inline Vector Vector::operator*(float fl) const
{
	Vector res;
	VectorMultiply(*this, fl, res);
	return res;
}

inline Vector operator*(const float lhs, const Vector &rhs)
{
	Vector res;
	VectorMultiply(rhs, lhs, res);
	return res;
}

inline Vector Vector::operator*(const Vector &v) const
{
	Vector res;
	VectorMultiply(*this, v, res);
	return res;
}

inline Vector &Vector::operator=(const Vector &vOther)
{
	x = vOther.x; y = vOther.y; z = vOther.z;
	return *this;
}

inline Vector Vector::operator+(const Vector &v) const
{
	Vector res;
	VectorAdd(*this, v, res);
	return res;
}

inline Vector Vector::operator-(void) const
{
	return Vector(-x, -y, -z);
}

FORCEINLINE void VectorDivide(const Vector &a, vec_t b, Vector &c)
{
	vec_t oob = 1.0f / b;
	c.x = a.x * oob;
	c.y = a.y * oob;
	c.z = a.z * oob;
}

inline Vector Vector::operator/(float fl) const
{
	Vector res;
	VectorDivide(*this, fl, res);
	return res;
}

FORCEINLINE  Vector &Vector::operator+=(const Vector &v)
{
	x += v.x; y += v.y; z += v.z;
	return *this;
}

FORCEINLINE  Vector &Vector::operator-=(const Vector &v)
{
	x -= v.x; y -= v.y; z -= v.z;
	return *this;
}

inline Vector Vector::operator-(const Vector &v) const
{
	Vector res;
	VectorSubtract(*this, v, res);
	return res;
}

FORCEINLINE  Vector &Vector::operator*=(float fl)
{
	x *= fl;
	y *= fl;
	z *= fl;
	return *this;
}

FORCEINLINE  Vector &Vector::operator/=(float fl)
{
	float oofl = 1.0f / fl;
	x *= oofl;
	y *= oofl;
	z *= oofl;
	return *this;
}



void inline SinCos(float radians, float *sine, float *cosine)
{
	*sine = sin(radians);
	*cosine = cos(radians);
}

enum
{
	PITCH = 0,	// up / down
	YAW,		// left / right
	ROLL		// fall over
};


FORCEINLINE vec_t DotProduct(const Vector &a, const Vector &b)
{
	return(a.x * b.x + a.y * b.y + a.z * b.z);
}

FORCEINLINE vec_t DotProduct2D(const Vector &a, const Vector &b)
{
	return(a.x * b.x + a.y * b.y);
}

inline void CrossProduct(const Vector &a, const Vector &b, Vector &result)
{
	result.x = a.y * b.z - a.z * b.y;
	result.y = a.z * b.x - a.x * b.z;
	result.z = a.x * b.y - a.y * b.x;
}

inline Vector CrossProduct(const Vector &a, const Vector &b)
{
	Vector result{};
	result.x = a.y * b.z - a.z * b.y;
	result.y = a.z * b.x - a.x * b.z;
	result.z = a.x * b.y - a.y * b.x;
	return result;
}

inline vec_t VectorLength(const Vector &v)
{
	return (vec_t)sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline vec_t VectorNormalize(Vector &v)
{
	vec_t l = VectorLength(v);
	if (l != 0.0f)
	{
		v /= l;
	}
	else
	{
		v.x = v.y = 0.0f; v.z = 1.0f;
	}
	return l;
}

inline Vector VectorRotate(const Vector &v, const Vector &k, float degrees)
{
	// Rodrigues rotation
	float radians = degrees * 3.14159265 / 180;

	Vector crossProduct;
	CrossProduct(k, v, crossProduct);

	return v*cos(radians) + crossProduct * sin(radians) + k * DotProduct(k,v) * (1-cos(radians));
}

inline void VectorPivotXY(Vector &point, const Vector &pivot, float degrees)
{
	float s = sin(degrees * 3.14159265 / 180);
	float c = cos(degrees * 3.14159265 / 180);
	point.x -= pivot.x;
	point.y -= pivot.y;
	float xnew = point.x * c - point.y * s;
	float ynew = point.x * s + point.y * c;
	point.x = xnew + pivot.x;
	point.y = ynew + pivot.y;
}

class QAngleByValue;

typedef float vec_t;

class QAngle
{
public:
	// Members
	vec_t x, y, z;

	// Construction/destruction
	QAngle();
	QAngle(vec_t X, vec_t Y, vec_t Z);
	//	QAngle(RadianEuler const &angles);	// evil auto type promotion!!!

	// Allow pass-by-value
	operator QAngleByValue &() { return *((QAngleByValue *)(this)); }
	operator const QAngleByValue &() const { return *((const QAngleByValue *)(this)); }

	// Initialization
	void Init(vec_t ix = 0.0f, vec_t iy = 0.0f, vec_t iz = 0.0f);
	void Random(vec_t minVal, vec_t maxVal);

	// Got any nasty NAN's?
	bool IsValid() const;
	void Invalidate();

	// array access...
	vec_t operator[](int i) const;
	vec_t &operator[](int i);

	// Base address...
	vec_t *Base();
	vec_t const *Base() const;

	// equality
	bool operator==(const QAngle &v) const;
	bool operator!=(const QAngle &v) const;

	// arithmetic operations
	QAngle &operator+=(const QAngle &v);
	QAngle &operator-=(const QAngle &v);
	QAngle &operator*=(float s);
	QAngle &operator/=(float s);

	// Get the vector's magnitude.
	vec_t	Length() const;
	vec_t	LengthSqr() const;

	void Normalize();

	// negate the QAngle components
	//void	Negate(); 

	// No assignment operators either...
	QAngle &operator=(const QAngle &src);

#ifndef VECTOR_NO_SLOW_OPERATIONS
	// copy constructors

	// arithmetic operations
	QAngle	operator-(void) const;

	QAngle	operator+(const QAngle &v) const;
	QAngle	operator-(const QAngle &v) const;
	QAngle	operator*(float fl) const;
	QAngle	operator/(float fl) const;
#else

private:
	// No copy constructors allowed if we're in optimal mode
	QAngle(const QAngle &vOther);

#endif

	static void AngleVectors(const QAngle &angles, Vector *forward, Vector *right, Vector *up);
	static void VectorAngles(const Vector &forward, QAngle &angles);
	static void VectorAngles(const Vector &forward, const Vector &pseudoup, QAngle &angles);
};



inline QAngle::QAngle()
{
#ifdef _DEBUG
#ifdef VECTOR_PARANOIA
	// Initialize to NAN to catch errors
	x = y = z = VEC_T_NAN;
#endif
#endif
}

inline void QAngle::Init(vec_t ix, vec_t iy, vec_t iz)
{
	x = ix; y = iy; z = iz;
}


inline QAngle::QAngle(vec_t X, vec_t Y, vec_t Z)
{
	x = X; y = Y; z = Z;
}

inline vec_t &QAngle::operator[](int i)
{
	return ((vec_t *)this)[i];
}

inline vec_t QAngle::operator[](int i) const
{
	return ((vec_t *)this)[i];
}

inline QAngle &QAngle::operator=(const QAngle &vOther)
{
	x = vOther.x; y = vOther.y; z = vOther.z;
	return *this;
}

inline QAngle QAngle::operator-(void) const
{
	QAngle ret(-x, -y, -z);
	return ret;
}

inline QAngle QAngle::operator+(const QAngle &v) const
{
	QAngle res;
	res.x = x + v.x;
	res.y = y + v.y;
	res.z = z + v.z;
	return res;
}

inline QAngle QAngle::operator-(const QAngle &v) const
{
	QAngle res;
	res.x = x - v.x;
	res.y = y - v.y;
	res.z = z - v.z;
	return res;
}

inline bool QAngle::operator!=(const QAngle& src) const
{
	return (src.x != this->x) || (src.y != this->y) || (src.z != this->z);
}

/*inline QAngle& QAngle::operator+=(const QAngle& v)
{
	QAngle res;
	res.x = x + v.x;
	res.y = y + v.y;
	res.z = z + v.z;
	return res;
}*/

inline float AngleNormalize(float angle) {
	//return 360 * std::floor((angle + 180) / 360)

	if (angle > 180)
		return angle - 360;
	else if (angle < -180)
		return angle + 360;

	return angle;
}

inline void QAngle::Normalize()
{
	this->x = AngleNormalize(this->x);
	this->y = AngleNormalize(this->y);
	this->z = AngleNormalize(this->z);
}

inline void QAngle::AngleVectors(const QAngle &angles, Vector *forward, Vector *right, Vector *up)
{

	float sr, sp, sy, cr, cp, cy;

	SinCos(DEG2RAD(angles[YAW]), &sy, &cy);
	SinCos(DEG2RAD(angles[PITCH]), &sp, &cp);
	SinCos(DEG2RAD(angles[ROLL]), &sr, &cr);

	if (forward)
	{
		forward->x = cp * cy;
		forward->y = cp * sy;
		forward->z = -sp;
	}

	if (right)
	{
		right->x = (-1 * sr * sp * cy + -1 * cr * -sy);
		right->y = (-1 * sr * sp * sy + -1 * cr * cy);
		right->z = -1 * sr * cp;
	}

	if (up)
	{
		up->x = (cr * sp * cy + -sr * -sy);
		up->y = (cr * sp * sy + -sr * cy);
		up->z = cr * cp;
	}
}

//-----------------------------------------------------------------------------
// Forward direction vector -> Euler angles
//-----------------------------------------------------------------------------
inline void QAngle::VectorAngles(const Vector &forward, QAngle &angles)
{
	float	tmp, yaw, pitch;

	if (forward[1] == 0 && forward[0] == 0)
	{
		yaw = 0;
		if (forward[2] > 0)
			pitch = 270;
		else
			pitch = 90;
	}
	else
	{
		yaw = (atan2(forward[1], forward[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		tmp = sqrt(forward[0] * forward[0] + forward[1] * forward[1]);
		pitch = (atan2(-forward[2], tmp) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	angles[0] = pitch;
	angles[1] = yaw;
	angles[2] = 0;
}
//-----------------------------------------------------------------------------
// Forward direction vector with a reference up vector -> Euler angles
//-----------------------------------------------------------------------------
inline void QAngle::VectorAngles(const Vector &forward, const Vector &pseudoup, QAngle &angles)
{
	Vector left;

	CrossProduct(pseudoup, forward, left);
	VectorNormalize(left);

	float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

	// enough here to get angles?
	if (xyDist > 0.001f)
	{
		// (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
		angles[1] = RAD2DEG(atan2f(forward[1], forward[0]));

		// The engine does pitch inverted from this, but we always end up negating it in the DLL
		// UNDONE: Fix the engine to make it consistent
		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		float up_z = (left[1] * forward[0]) - (left[0] * forward[1]);

		// (roll)	z = ATAN( left.z, up.z );
		angles[2] = RAD2DEG(atan2f(left[2], up_z));
	}
	else	// forward is mostly Z, gimbal lock-
	{
		// (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
		angles[1] = RAD2DEG(atan2f(-left[0], left[1])); //This was originally copied from the "void MatrixAngles( const matrix3x4_t& matrix, float *angles )" code, and it's 180 degrees off, negated the values and it all works now (Dave Kircher)

		// The engine does pitch inverted from this, but we always end up negating it in the DLL
		// UNDONE: Fix the engine to make it consistent
		// (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
		angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

		// Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
		angles[2] = 0;
	}
}
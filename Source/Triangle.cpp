//
// Triangle.cpp
//
// Clark Kromenaker
//
#include "Triangle.h"

#include "Debug.h"
#include "LineSegment.h"
#include "Plane.h"

Triangle::Triangle(const Vector3& p0, const Vector3& p1, const Vector3& p2) :
	p0(p0),
	p1(p1),
	p2(p2)
{
	//TODO: What if the points don't form a triangle???
}

bool Triangle::ContainsPoint(const Vector3& point) const
{
	return ContainsPoint(p0, p1, p2, point);
}

Vector3 Triangle::GetClosestPoint(const Vector3& point) const
{
	return GetClosestPoint(p0, p1, p2, point);
}

/*static*/ bool Triangle::ContainsPoint(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& point)
{
	// This uses the "testing normals" method outlined here:
	// https://gdbooks.gitbooks.io/3dcollisions/content/Chapter4/point_in_triangle.html
	
	// Create vectors from the point to each point of the triangle.
	// Every pair of these vectors form two sides of a triangle, with the third side being from the original p0/p1/p2 triangle.
	// So the "big" triangle is split into 3 smaller triangles (if the point is inside the triangle).
	// A bit hard to visualize, I know!
	Vector3 pointToP0 = p0 - point;
	Vector3 pointToP1 = p1 - point;
	Vector3 pointToP2 = p2 - point;

	// Each pair of above vectors form two sides of a triangle.
	// So, we can take the cross product with each pair to get the normal of each triangle.
	Vector3 u = Vector3::Cross(pointToP1, pointToP2);
	Vector3 v = Vector3::Cross(pointToP2, pointToP0);
	Vector3 w = Vector3::Cross(pointToP0, pointToP1);
	
	// If normals are not facing the same way, it means the point is not in the triangle.
	if(Vector3::Dot(u, v) < 0.0f || Vector3::Dot(u, w) < 0.0f) { return false; }
	
	// Normals are all facing the same way - so, this point is in the triangle!
	return true;
}

/*static*/ Vector3 Triangle::GetClosestPoint(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& point)
{
	// Create a plane from this triangle.
	// Find closest point on the plane to our point. That becomes our new test point.
	Plane plane(p0, p1, p2);
	Vector3 testPoint = plane.GetClosestPoint(point);
	//Debug::DrawLine(Vector3::Zero, testPoint, Color32::Green);
	
	// If the point is in the triangle, then we're already done!
	if(ContainsPoint(p0, p1, p2, testPoint))
	{
		return testPoint;
	}
	
	// Test point is not in the triangle!
	// We must determine what edge of the triangle the point is closest to.
	// Start by creating line segments representing each side of the triangle.
	LineSegment p0ToP1(p0, p1);
	LineSegment p1ToP2(p1, p2);
	LineSegment p2ToP0(p2, p0);
	
	// Get closest point on each line segment to our test point.
	Vector3 closestPointP0ToP1 = p0ToP1.GetClosestPoint(testPoint);
	Vector3 closestPointP1ToP2 = p1ToP2.GetClosestPoint(testPoint);
	Vector3 closestPointP2ToP0 = p2ToP0.GetClosestPoint(testPoint);
	//Debug::DrawLine(Vector3::Zero, closestPointP0ToP1, Color32::Magenta);
	//Debug::DrawLine(Vector3::Zero, closestPointP1ToP2, Color32::Magenta);
	//Debug::DrawLine(Vector3::Zero, closestPointP2ToP0, Color32::Magenta);
	
	// Get distance from test point to each of those closest points.
	// The shortest distance means that is the closest point on the triangle!
	float p0ToP1DistSq = (testPoint - closestPointP0ToP1).GetLengthSq();
	float p1ToP2DistSq = (testPoint - closestPointP1ToP2).GetLengthSq();
	float p2ToP0DistSq = (testPoint - closestPointP2ToP0).GetLengthSq();
	
	// Whichever dist is smallest indicates the closest point on the triangle.
	float min = Math::Min(p0ToP1DistSq, Math::Min(p1ToP2DistSq, p2ToP0DistSq));
	if(Math::AreEqual(min, p0ToP1DistSq))
	{
		return closestPointP0ToP1;
	}
	else if(Math::AreEqual(min, p1ToP2DistSq))
	{
		return closestPointP1ToP2;
	}
	return closestPointP2ToP0;
}

void Triangle::DebugDraw(const Color32& color, float duration, const Matrix4* transformMatrix) const
{
	if(transformMatrix != nullptr)
	{
		Vector3 t0 = transformMatrix->TransformPoint(p0);
		Vector3 t1 = transformMatrix->TransformPoint(p1);
		Vector3 t2 = transformMatrix->TransformPoint(p2);
		
		Debug::DrawLine(t0, t1, color, duration);
		Debug::DrawLine(t1, t2, color, duration);
		Debug::DrawLine(t2, t0, color, duration);
	}
	else
	{
		Debug::DrawLine(p0, p1, color, duration);
		Debug::DrawLine(p1, p2, color, duration);
		Debug::DrawLine(p2, p0, color, duration);
	}
}

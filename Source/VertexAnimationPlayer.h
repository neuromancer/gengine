//
// VertexAnimationPlayer.h
//
// Clark Kromenaker
//
// A component that can play a vertex animation on an actor.
//
#pragma once
#include "Component.h"

class MeshRenderer;
class VertexAnimation;

class VertexAnimationPlayer : public Component
{
	TYPE_DECL_CHILD();
public:
	VertexAnimationPlayer(Actor* owner);
	
	void Play(VertexAnimation* animation) { mVertexAnimation = animation; mVertexAnimationTimer = 0.0f; }
	
protected:
	void UpdateInternal(float deltaTime) override;
	
private:
	// The mesh renderer from which we will animate.
	MeshRenderer* mMeshRenderer = nullptr;
	
	// How many frames per second to run at. Default is 15 (from GK3 docs).
	int mFramesPerSecond = 15;
	
	// If defined, a currently running vertex animation.
	VertexAnimation* mVertexAnimation = nullptr;
	
	// Timer for tracking progress on vertex animation.
	float mVertexAnimationTimer = 0.0f;
};

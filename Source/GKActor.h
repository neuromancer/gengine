//
// GKActor.cpp
//
// Clark Kromenaker
//
// An "actor" in the GK3 system. An actor is considered to be
// one of the characters - like Gabe, Grace, Mosely. An actor is an entity
// that can walk around, animate, perform facial expressions and eye contact,
// play sound effects, etc.
//
// There are definitely "tiers" of actors - like, Gabe is an actor, but so
// are the "cat" and "chicken". So, maybe we need a subclass for humanoids at some point.
//
#pragma once
#include "Actor.h"

#include <string>

#include "Heading.h"

class Animation;
class FaceController;
class GAS;
class GasPlayer;
class MeshRenderer;
class VertexAnimation;
class VertexAnimationPlayer;
class Walker;

class GKActor : public Actor
{
public:
    enum class FidgetType
    {
		None,
        Idle,
        Talk,
        Listen,
		Custom
    };
	
	GKActor();
	GKActor(std::string identifier);
	
	void SetIdentifier(std::string identifier) { mIdentifier = identifier; }
	const std::string& GetIdentifier() const { return mIdentifier; }
	
	void SetNoun(std::string noun) { mNoun = noun; }
	std::string GetNoun() const { return mNoun; }
	
    void SetIdleGas(GAS* gas) { mIdleGas = gas; }
    void SetTalkGas(GAS* gas) { mTalkGas = gas; }
    void SetListenGas(GAS* gas) { mListenGas = gas; }
	
	void StartFidget(FidgetType type);
	void StartFidget(GAS* gas);
	
	void PlayAnimation(VertexAnimation* animation);
	void PlayAnimation(VertexAnimation* animation, int framesPerSecond);
	void SampleAnimation(VertexAnimation* animation, int frame);
	
    MeshRenderer* GetMeshRenderer() const { return mMeshRenderer; }
	Walker* GetWalker() const { return mWalker; }
	FaceController* GetFaceController() const { return mFaceController; }
	
	void SetHeading(const Heading& heading);
	Heading GetHeading() const;
	
protected:
	void OnUpdate(float deltaTime) override;
	
private:
	// The character's 3-letter identifier (GAB, GRA, etc).
	std::string mIdentifier;
	
	// This character's noun.
	std::string mNoun;
	
    // Actor's current fidget.
    FidgetType mActiveFidget = FidgetType::Idle;
	
	// The character's mesh renderer.
	MeshRenderer* mMeshRenderer = nullptr;
	
	// The character's animation player.
	VertexAnimationPlayer* mAnimationPlayer = nullptr;
	
	// The character's walking control.
	Walker* mWalker = nullptr;
	
	// The character's face control.
	FaceController* mFaceController;
	
	// Player for GAS logic.
	GasPlayer* mGasPlayer = nullptr;
    
    // GAS scripts to use when actor is idle, talking, or listening.
    GAS* mIdleGas = nullptr;
    GAS* mTalkGas = nullptr;
    GAS* mListenGas = nullptr;
};

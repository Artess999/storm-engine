// ============================================================================================
// Spirenkov Maxim aka Sp-Max Shaman, 2001
// --------------------------------------------------------------------------------------------
// Storm engine v2.00
// --------------------------------------------------------------------------------------------
// AnimationImp
// --------------------------------------------------------------------------------------------
// Implementing the Animation interface
// ============================================================================================

#ifndef _AnimationManagerImp_H_
#define _AnimationManagerImp_H_

#include "ActionPlayerImp.h"
#include "Animation.h"
#include "AnimationInfo.h"
#include "AnimationTimerImp.h"

#define ANIIMP_MAXLISTENERS 8

class AnimationServiceImp;

class AnimationImp final : public Animation
{
    // --------------------------------------------------------------------------------------------
    // Construction, destruction
    // --------------------------------------------------------------------------------------------
  public:
    AnimationImp(long id, AnimationInfo *animationInfo);
    ~AnimationImp();

    // Set pointer to animation service
    static void SetAnimationService(AnimationServiceImp *animationService);

    //--------------------------------------------------------------------------------------------
    // Animation
    //--------------------------------------------------------------------------------------------
  public:
    // Access the action player
    virtual ActionPlayer &Player(long index);
    // Access the animation timer
    virtual AnimationTimer &Timer(long index);
    // Events
    // Set internal event
    virtual long SetEvent(AnimationEvent event, long index, AnimationEventListener *ael);
    // Delete internal event
    virtual void DelEvent(long eventID);
    // Set an external event handler
    virtual void SetEventListener(AnimationEventListener *ael);
    // Access to bones
    // Get the number of bones in a skeleton
    virtual long GetNumBones() const;
    // Get animation matrix for bone
    virtual CMatrix &GetAnimationMatrix(long iBone) const;
    // misc
    // Get custom data for animation
    virtual const char *GetData(const char *dataName) const;
    // Copy the state of one player to another
    virtual void CopyPlayerState(long indexSrc, long indexDst, bool copyTimerState = false);
    // Get animation speed
    virtual float GetFPS();
    // Set blending modes
    // Automatic normalization of blending coefficients
    virtual bool SetAutoNormalize(bool isNormalize = true);
    virtual bool GetAutoNormalize();
    // Allow custom blending coefficients in ActionPlayer
    virtual bool UserBlend(bool isBlend = true);
    virtual bool IsUserBlend();

    //--------------------------------------------------------------------------------------------
    // AnimationImp
    //--------------------------------------------------------------------------------------------
  public:
    // Get thisID
    long GetThisID();
    // Get pointer to AnimationInfo
    AnimationInfo *GetAnimationInfo();
    // Find action by name
    ActionInfo *GetActionInfo(const char *actionName);
    // Take a step in time
    void Execute(long dltTime);
    // Calculate animation matrices
    void BuildAnimationMatrices();
    // Get a pointer to the animation srvis
    static AnimationServiceImp *GetAniService();
    // AnimationPlayer events
    // A new action has been set for the player
    void ApeSetnewaction(long index);
    // Action playback enabled
    void ApePlay(long index);
    // Paused playing an action
    void ApePauseOn(long index);
    // Continued playing action
    void ApePauseOff(long index);
    // Stop playing an action
    void ApeStop(long index);
    // Generate event on every frame
    void ApeAlways(long index);
    // On the action starts
    void ApeStart(long index);
    // On the action ends
    void ApeEnd(long index);
    // On changing the direction of playback (pingpong)
    void ApeChange(long index);
    // Timer has ended or has been stopped
    void AteTimerstop(long index);

    // External event
    void AteExtern(long plIndex, const char *evt);

  private:
    // Send events
    void SendEvent(AnimationEvent event, long index);

    // --------------------------------------------------------------------------------------------
    // Encapsulation
    // --------------------------------------------------------------------------------------------
  private:
    // identifier
    long thisID;
    // Animation pointer
    AnimationInfo *aniInfo;
    // Animations
    ActionPlayerImp action[ANI_MAX_ACTIONS];
    // Timers
    AnimationTimerImp timer[ANI_MAX_ACTIONS];
    // Auto normalization
    bool isAutoNormalize;
    // Custom blending
    bool isUserBlend;
    // Skeleton matrices
    CMatrix *matrix;
    // Internal event subscribers
    AnimationEventListener *ae_listeners[ae_numevents][ANIIMP_MAXLISTENERS];
    // Subscribers to external events
    AnimationEventListener *ae_listenersExt;
    // Animation Service Pointer
    static AnimationServiceImp *aniService;
};

//============================================================================================
// inline
//============================================================================================

// Set pointer to animation service
inline void AnimationImp::SetAnimationService(AnimationServiceImp *animationService)
{
    Assert(aniService == nullptr);
    aniService = animationService;
}

//--------------------------------------------------------------------------------------------
// AnimationImp
//--------------------------------------------------------------------------------------------

// Get thisID
inline long AnimationImp::GetThisID()
{
    return thisID;
}

// Get pointer to AnimationInfo
inline AnimationInfo *AnimationImp::GetAnimationInfo()
{
    return aniInfo;
}

// Find action by name
inline ActionInfo *AnimationImp::GetActionInfo(const char *actionName)
{
    return aniInfo->FindAction(actionName);
}

// Get a pointer to the animation service
inline AnimationServiceImp *AnimationImp::GetAniService()
{
    return aniService;
}

// AnimationPlayer events
// A new action has been set for the player
inline void AnimationImp::ApeSetnewaction(long index)
{
    SendEvent(ae_setnewaction, index);
}

// Action playback enabled
inline void AnimationImp::ApePlay(long index)
{
    SendEvent(ae_play, index);
}

// Paused playing an action
inline void AnimationImp::ApePauseOn(long index)
{
    SendEvent(ae_pauseon, index);
}

// Continued playing an action
inline void AnimationImp::ApePauseOff(long index)
{
    SendEvent(ae_pauseoff, index);
}

// Stop playing an action
inline void AnimationImp::ApeStop(long index)
{
    SendEvent(ae_stop, index);
}

// When the action starts
inline void AnimationImp::ApeStart(long index)
{
    SendEvent(ae_start, index);
}

// When the action ends
inline void AnimationImp::ApeEnd(long index)
{
    SendEvent(ae_end, index);
}

// When changing the direction of playback (pingpong)
inline void AnimationImp::ApeChange(long index)
{
    SendEvent(ae_change, index);
}

// Timer has ended or has been stopped
inline void AnimationImp::AteTimerstop(long index)
{
    SendEvent(ae_timerstop, index);
}

// External event
inline void AnimationImp::AteExtern(long plIndex, const char *evt)
{
    if (ae_listenersExt)
        ae_listenersExt->Event(this, plIndex, evt);
}

#endif
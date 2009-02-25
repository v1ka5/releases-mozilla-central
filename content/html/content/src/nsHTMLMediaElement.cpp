/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: ML 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla code.
 *
 * The Initial Developer of the Original Code is the Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Chris Double <chris.double@double.co.nz>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */
#include "nsIDOMHTMLMediaElement.h"
#include "nsIDOMHTMLSourceElement.h"
#include "nsHTMLMediaElement.h"
#include "nsGenericHTMLElement.h"
#include "nsPresContext.h"
#include "nsIPresShell.h"
#include "nsGkAtoms.h"
#include "nsSize.h"
#include "nsIFrame.h"
#include "nsIDocument.h"
#include "nsIDOMDocument.h"
#include "nsDOMError.h"
#include "nsNodeInfoManager.h"
#include "plbase64.h"
#include "nsNetUtil.h"
#include "prmem.h"
#include "nsNetUtil.h"
#include "nsXPCOMStrings.h"
#include "prlock.h"
#include "nsThreadUtils.h"
#include "nsContentUtils.h"

#include "nsIScriptSecurityManager.h"
#include "nsIXPConnect.h"
#include "jsapi.h"

#include "nsIRenderingContext.h"
#include "nsITimer.h"

#include "nsEventDispatcher.h"
#include "nsIDOMDocumentEvent.h"
#include "nsIDOMProgressEvent.h"
#include "nsHTMLMediaError.h"
#include "nsICategoryManager.h"
#include "nsCommaSeparatedTokenizer.h"

#include "nsIContentPolicy.h"
#include "nsContentPolicyUtils.h"
#include "nsContentErrors.h"
#include "nsCrossSiteListenerProxy.h"

#ifdef MOZ_OGG
#include "nsOggDecoder.h"
#endif
#ifdef MOZ_WAVE
#include "nsWaveDecoder.h"
#endif


class nsMediaEvent : public nsRunnable
{
public:

  nsMediaEvent(nsHTMLMediaElement* aElement) :
    mElement(aElement),
    mCurrentLoad(mElement->GetCurrentMediaLoad()) {}
  ~nsMediaEvent() {}

  NS_IMETHOD Run() = 0;

protected:
  PRBool IsCancelled() {
    return mElement->GetCurrentMediaLoad() != mCurrentLoad;
  }

  nsCOMPtr<nsHTMLMediaElement> mElement;
  nsRefPtr<nsMediaLoad> mCurrentLoad;
};


class nsAsyncEventRunner : public nsMediaEvent
{
private:
  nsString mName;
  PRPackedBool mProgress;
  
public:
  nsAsyncEventRunner(const nsAString& aName, nsHTMLMediaElement* aElement, PRBool aProgress) : 
    nsMediaEvent(aElement), mName(aName), mProgress(aProgress)
  {
  }
  
  NS_IMETHOD Run() {
    // Silently cancel if our load has been cancelled.
    if (IsCancelled())
      return NS_OK;
    return mProgress ?
      mElement->DispatchProgressEvent(mName) :
      mElement->DispatchSimpleEvent(mName);
  }
};

class nsMediaLoadEvent : public nsMediaEvent {
public:
  nsMediaLoadEvent(nsHTMLMediaElement *aElement)
    : nsMediaEvent(aElement) {}
  NS_IMETHOD Run() {
    if (!IsCancelled())
      mElement->Load();
    return NS_OK;
  }
};

class nsHTMLMediaElement::LoadNextCandidateEvent : public nsMediaEvent {
public:
  LoadNextCandidateEvent(nsHTMLMediaElement *aElement)
    : nsMediaEvent(aElement) {}
  NS_IMETHOD Run() {
    if (!IsCancelled())
      mElement->LoadNextCandidate();
    return NS_OK;
  }
};

void nsHTMLMediaElement::QueueLoadTask()
{
  nsCOMPtr<nsIRunnable> event = new nsMediaLoadEvent(this);
  NS_DispatchToMainThread(event);
}

void nsHTMLMediaElement::QueueLoadNextCandidateTask()
{
  nsCOMPtr<nsIRunnable> event = new LoadNextCandidateEvent(this);
  NS_DispatchToMainThread(event);
}

class nsHTMLMediaElement::MediaLoadListener : public nsIStreamListener
{
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

public:
  MediaLoadListener(nsHTMLMediaElement* aElement)
    : mElement(aElement)
  {
    NS_ABORT_IF_FALSE(mElement, "Must pass an element to call back");
  }

private:
  nsRefPtr<nsHTMLMediaElement> mElement;
  nsCOMPtr<nsIStreamListener> mNextListener;
};

NS_IMPL_ISUPPORTS2(nsHTMLMediaElement::MediaLoadListener, nsIRequestObserver, nsIStreamListener)

NS_IMETHODIMP nsHTMLMediaElement::MediaLoadListener::OnStartRequest(nsIRequest* aRequest, nsISupports* aContext)
{
  nsresult rv = NS_OK;

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (channel &&
      mElement &&
      NS_SUCCEEDED(rv = mElement->InitializeDecoderForChannel(channel, getter_AddRefs(mNextListener))) &&
      mNextListener) {
    rv = mNextListener->OnStartRequest(aRequest, aContext);
  } else {
    // If InitializeDecoderForChannel() returned an error, fire a network
    // error.
    if (NS_FAILED(rv) && !mNextListener && mElement) {
      // Load failed, attempt to load the next candidate resource. If there
      // are none, this will trigger a MEDIA_ERR_NONE_SUPPORTED error.
      mElement->QueueLoadNextCandidateTask();
    }
    // If InitializeDecoderForChannel did not return a listener (but may
    // have otherwise succeeded), we abort the connection since we aren't
    // interested in keeping the channel alive ourselves.
    rv = NS_BINDING_ABORTED;
  }

  // The element is only needed until we've had a chance to call
  // InitializeDecoderForChannel.
  mElement = nsnull;

  return rv;
}

NS_IMETHODIMP nsHTMLMediaElement::MediaLoadListener::OnStopRequest(nsIRequest* aRequest, nsISupports* aContext,
                                                                     nsresult aStatus)
{
  if (mNextListener) {
    return mNextListener->OnStopRequest(aRequest, aContext, aStatus);
  }
  return NS_OK;
}

NS_IMETHODIMP nsHTMLMediaElement::MediaLoadListener::OnDataAvailable(nsIRequest* aRequest, nsISupports* aContext,
                                                                       nsIInputStream* aStream, PRUint32 aOffset,
                                                                       PRUint32 aCount)
{
  NS_ABORT_IF_FALSE(mNextListener, "Must have a listener");
  return mNextListener->OnDataAvailable(aRequest, aContext, aStream, aOffset, aCount);
}

void
nsMediaLoad::AddCandidate(nsIURI *aURI)
{
  mCandidates.AppendObject(aURI);
}

already_AddRefed<nsIURI>
nsMediaLoad::GetNextCandidate()
{
  if (mPosition == mCandidates.Count())
    return nsnull;
  nsCOMPtr<nsIURI> uri = mCandidates.ObjectAt(mPosition);
  mPosition++;
  return uri.forget();
}

NS_IMPL_ISUPPORTS0(nsMediaLoad)

// nsIDOMHTMLMediaElement
NS_IMPL_URI_ATTR(nsHTMLMediaElement, Src, src)
NS_IMPL_BOOL_ATTR(nsHTMLMediaElement, Controls, controls)
NS_IMPL_BOOL_ATTR(nsHTMLMediaElement, Autoplay, autoplay)

/* readonly attribute nsIDOMHTMLMediaElement mozAutoplayEnabled; */
NS_IMETHODIMP nsHTMLMediaElement::GetMozAutoplayEnabled(PRBool *aAutoplayEnabled)
{
  *aAutoplayEnabled = mAutoplayEnabled;

  return NS_OK;
}

/* readonly attribute nsIDOMHTMLMediaError error; */
NS_IMETHODIMP nsHTMLMediaElement::GetError(nsIDOMHTMLMediaError * *aError)
{
  NS_IF_ADDREF(*aError = mError);

  return NS_OK;
}

/* readonly attribute boolean ended; */
NS_IMETHODIMP nsHTMLMediaElement::GetEnded(PRBool *aEnded)
{
  *aEnded = mDecoder ? mDecoder->IsEnded() : PR_FALSE;

  return NS_OK;
}

/* readonly attribute DOMString currentSrc; */
NS_IMETHODIMP nsHTMLMediaElement::GetCurrentSrc(nsAString & aCurrentSrc)
{
  nsCAutoString src;
  
  if (mDecoder) {
    nsCOMPtr<nsIURI> uri;
    mDecoder->GetCurrentURI(getter_AddRefs(uri));
    if (uri) {
      uri->GetSpec(src);
    }
  }

  aCurrentSrc = NS_ConvertUTF8toUTF16(src);

  return NS_OK;
}

/* readonly attribute unsigned short networkState; */
NS_IMETHODIMP nsHTMLMediaElement::GetNetworkState(PRUint16 *aNetworkState)
{
  *aNetworkState = mNetworkState;

  return NS_OK;
}

PRBool nsHTMLMediaElement::AbortExistingLoads()
{
  // Set a new load object. This will cause events which were enqueued
  // with a differnet load object to silently be cancelled.
  mCurrentLoad = new nsMediaLoad();
  nsRefPtr<nsMediaLoad> currentLoad = mCurrentLoad;

  if (mDecoder) {
    mDecoder->Shutdown();
    mDecoder = nsnull;
  }

  if (mBegun) {
    mBegun = PR_FALSE;
    mError = new nsHTMLMediaError(nsIDOMHTMLMediaError::MEDIA_ERR_ABORTED);
    DispatchProgressEvent(NS_LITERAL_STRING("abort"));
    if (mCurrentLoad != currentLoad) {
      // A new load was triggered in handler, bail out of this load.
      return PR_TRUE;
    }
  }

  mError = nsnull;
  mLoadedFirstFrame = PR_FALSE;
  mAutoplaying = PR_TRUE;

  // TODO: The playback rate must be set to the default playback rate.

  if (mNetworkState != nsIDOMHTMLMediaElement::NETWORK_EMPTY) {
    mNetworkState = nsIDOMHTMLMediaElement::NETWORK_EMPTY;
    ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_NOTHING);
    mPaused = PR_TRUE;
    // TODO: The current playback position must be set to 0.
    // TODO: The currentLoop DOM attribute must be set to 0.
    DispatchSimpleEvent(NS_LITERAL_STRING("emptied"));
    if (mCurrentLoad != currentLoad) {
      // A new load was triggered in handler, bail out of this load.
      return PR_TRUE;
    }
  }
  return PR_FALSE;
}

void nsHTMLMediaElement::NoSupportedMediaError()
{
  NS_ASSERTION(mCurrentLoad && !mCurrentLoad->HasMoreCandidates(),
               "Should have exhausted all candidates");
  mError = new nsHTMLMediaError(nsIDOMHTMLMediaError::MEDIA_ERR_NONE_SUPPORTED);
  mBegun = PR_FALSE;
  DispatchAsyncProgressEvent(NS_LITERAL_STRING("error"));
  mNetworkState = nsIDOMHTMLMediaElement::NETWORK_EMPTY;
  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("emptied"));
}

/* void load (); */
NS_IMETHODIMP nsHTMLMediaElement::Load()
{
  if (AbortExistingLoads())
    return NS_OK;

  mNetworkState = nsIDOMHTMLMediaElement::NETWORK_LOADING;
  mBegun = PR_TRUE;
  DispatchAsyncProgressEvent(NS_LITERAL_STRING("loadstart"));

  GenerateCandidates();

  QueueLoadNextCandidateTask();

  return NS_OK;
}

void nsHTMLMediaElement::LoadNextCandidate()
{
  NS_ASSERTION(mCurrentLoad, "Need a load object");

  while (mCurrentLoad->HasMoreCandidates()) {
    nsresult rv;
    nsCOMPtr<nsIURI> uri = mCurrentLoad->GetNextCandidate();

    if (mChannel) {
      mChannel->Cancel(NS_BINDING_ABORTED);
      mChannel = nsnull;
    }

    PRInt16 shouldLoad = nsIContentPolicy::ACCEPT;
    rv = NS_CheckContentLoadPolicy(nsIContentPolicy::TYPE_MEDIA,
                                   uri,
                                   NodePrincipal(),
                                   this,
                                   EmptyCString(), // mime type
                                   nsnull, // extra
                                   &shouldLoad,
                                   nsContentUtils::GetContentPolicy(),
                                   nsContentUtils::GetSecurityManager());
    if (NS_FAILED(rv) || NS_CP_REJECTED(shouldLoad)) continue;

    rv = NS_NewChannel(getter_AddRefs(mChannel),
                       uri,
                       nsnull,
                       nsnull,
                       nsnull,
                       nsIRequest::LOAD_NORMAL);
    if (NS_FAILED(rv)) continue;

    // The listener holds a strong reference to us.  This creates a reference
    // cycle which is manually broken in the listener's OnStartRequest method
    // after it is finished with the element.
    nsCOMPtr<nsIStreamListener> loadListener = new MediaLoadListener(this);
    if (!loadListener) continue;

    nsCOMPtr<nsIStreamListener> listener;
    if (ShouldCheckAllowOrigin()) {
      listener = new nsCrossSiteListenerProxy(loadListener,
                                              NodePrincipal(),
                                              mChannel, 
                                              PR_FALSE,
                                              &rv);
      if (!listener || NS_FAILED(rv)) continue;
    } else {
      rv = nsContentUtils::GetSecurityManager()->
             CheckLoadURIWithPrincipal(NodePrincipal(),
                                       uri,
                                       nsIScriptSecurityManager::STANDARD);
      if (NS_FAILED(rv)) continue;
      listener = loadListener;
    }

    nsCOMPtr<nsIHttpChannel> hc = do_QueryInterface(mChannel);
    if (hc) {
      // Use a byte range request from the start of the resource.
      // This enables us to detect if the stream supports byte range
      // requests, and therefore seeking, early.
      hc->SetRequestHeader(NS_LITERAL_CSTRING("Range"),
                           NS_LITERAL_CSTRING("bytes=0-"),
                           PR_FALSE);
    }

    rv = mChannel->AsyncOpen(listener, nsnull);
    if (NS_FAILED(rv)) {
      // OnStartRequest is guaranteed to be called if the open succeeds.  If
      // the open failed, the listener's OnStartRequest will never be called,
      // so we need to break the element->channel->listener->element reference
      // cycle here.  The channel holds the only reference to the listener,
      // and is useless now anyway, so drop our reference to it to allow it to
      // be destroyed.
      mChannel = nsnull;
      continue;
    }

    // Else the channel must be open and starting to download. If it encounters
    // a non-catestrophic failure, it will set a new task to continue loading
    // the candidates.
    return;
  }

  // Must have exhausted all candidates.
  NoSupportedMediaError();
}

nsresult nsHTMLMediaElement::LoadWithChannel(nsIChannel *aChannel,
                                             nsIStreamListener **aListener)
{
  NS_ENSURE_ARG_POINTER(aChannel);
  NS_ENSURE_ARG_POINTER(aListener);

  *aListener = nsnull;

  if (AbortExistingLoads())
    return NS_OK;

  nsresult rv = InitializeDecoderForChannel(aChannel, aListener);
  if (NS_FAILED(rv)) {
    return rv;
  }

  mBegun = PR_TRUE;

  DispatchAsyncProgressEvent(NS_LITERAL_STRING("loadstart"));

  return NS_OK;
}

/* readonly attribute unsigned short readyState; */
NS_IMETHODIMP nsHTMLMediaElement::GetReadyState(PRUint16 *aReadyState)
{
  *aReadyState = mReadyState;

  return NS_OK;
}

/* readonly attribute boolean seeking; */
NS_IMETHODIMP nsHTMLMediaElement::GetSeeking(PRBool *aSeeking)
{
  *aSeeking = mDecoder && mDecoder->IsSeeking();

  return NS_OK;
}

/* attribute float currentTime; */
NS_IMETHODIMP nsHTMLMediaElement::GetCurrentTime(float *aCurrentTime)
{
  *aCurrentTime = mDecoder ? mDecoder->GetCurrentTime() : 0.0;
  return NS_OK;
}

NS_IMETHODIMP nsHTMLMediaElement::SetCurrentTime(float aCurrentTime)
{
  if (!mDecoder)
    return NS_ERROR_DOM_INVALID_STATE_ERR;

  // Detect for a NaN and invalid values.
  if (!(aCurrentTime >= 0.0))
    return NS_ERROR_FAILURE;

  if (mReadyState == nsIDOMHTMLMediaElement::HAVE_NOTHING) 
    return NS_ERROR_DOM_INVALID_STATE_ERR;

  mPlayingBeforeSeek = IsPotentiallyPlaying();
  // The media backend is responsible for dispatching the timeupdate
  // event if it changes the playback position as a result of the seek.
  nsresult rv = mDecoder->Seek(aCurrentTime);
  return rv;
}

/* readonly attribute float duration; */
NS_IMETHODIMP nsHTMLMediaElement::GetDuration(float *aDuration)
{
  *aDuration =  mDecoder ? mDecoder->GetDuration() : 0.0;
  return NS_OK;
}

/* readonly attribute boolean paused; */
NS_IMETHODIMP nsHTMLMediaElement::GetPaused(PRBool *aPaused)
{
  *aPaused = mPaused;

  return NS_OK;
}

/* void pause (); */
NS_IMETHODIMP nsHTMLMediaElement::Pause()
{
  if (mNetworkState == nsIDOMHTMLMediaElement::NETWORK_EMPTY) {
    nsresult rv = Load();
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (mDecoder) {
    mDecoder->Pause();
  }

  PRBool oldPaused = mPaused;
  mPaused = PR_TRUE;
  mAutoplaying = PR_FALSE;
  
  if (!oldPaused) {
    DispatchAsyncSimpleEvent(NS_LITERAL_STRING("timeupdate"));
    DispatchAsyncSimpleEvent(NS_LITERAL_STRING("pause"));
  }

  return NS_OK;
}

/* attribute float volume; */
NS_IMETHODIMP nsHTMLMediaElement::GetVolume(float *aVolume)
{
  *aVolume = mVolume;

  return NS_OK;
}

NS_IMETHODIMP nsHTMLMediaElement::SetVolume(float aVolume)
{
  if (aVolume < 0.0f || aVolume > 1.0f)
    return NS_ERROR_DOM_INDEX_SIZE_ERR;

  if (aVolume == mVolume)
    return NS_OK;

  mVolume = aVolume;

  if (mDecoder && !mMuted)
    mDecoder->SetVolume(mVolume);

  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("volumechange"));

  return NS_OK;
}

/* attribute boolean muted; */
NS_IMETHODIMP nsHTMLMediaElement::GetMuted(PRBool *aMuted)
{
  *aMuted = mMuted;

  return NS_OK;
}

NS_IMETHODIMP nsHTMLMediaElement::SetMuted(PRBool aMuted)
{
  if (aMuted == mMuted)
    return NS_OK;

  mMuted = aMuted;

  if (mDecoder) {
    mDecoder->SetVolume(mMuted ? 0.0 : mVolume);
  }

  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("volumechange"));

  return NS_OK;
}

nsHTMLMediaElement::nsHTMLMediaElement(nsINodeInfo *aNodeInfo, PRBool aFromParser)
  : nsGenericHTMLElement(aNodeInfo),
    mNetworkState(nsIDOMHTMLMediaElement::NETWORK_EMPTY),
    mReadyState(nsIDOMHTMLMediaElement::HAVE_NOTHING),
    mVolume(1.0),
    mMediaSize(-1,-1),
    mBegun(PR_FALSE),
    mLoadedFirstFrame(PR_FALSE),
    mAutoplaying(PR_TRUE),
    mAutoplayEnabled(PR_TRUE),
    mPaused(PR_TRUE),
    mMuted(PR_FALSE),
    mIsDoneAddingChildren(!aFromParser),
    mPlayingBeforeSeek(PR_FALSE),
    mWaitingFired(PR_FALSE),
    mIsBindingToTree(PR_FALSE)
{
}

nsHTMLMediaElement::~nsHTMLMediaElement()
{
  if (mDecoder) {
    mDecoder->Shutdown();
    mDecoder = nsnull;
  }
  if (mChannel) {
    mChannel->Cancel(NS_BINDING_ABORTED);
    mChannel = nsnull;
  }
}

NS_IMETHODIMP nsHTMLMediaElement::Play()
{
  if (mNetworkState == nsIDOMHTMLMediaElement::NETWORK_EMPTY) {
    nsresult rv = Load();
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (mDecoder) {
    if (mDecoder->IsEnded()) {
      SetCurrentTime(0);
    }
    nsresult rv = mDecoder->Play();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // TODO: If the playback has ended, then the user agent must set 
  // currentLoop to zero and seek to the effective start.
  // TODO: The playback rate must be set to the default playback rate.
  if (mPaused) {
    DispatchAsyncSimpleEvent(NS_LITERAL_STRING("play"));
    switch (mReadyState) {
    case nsIDOMHTMLMediaElement::HAVE_METADATA:
    case nsIDOMHTMLMediaElement::HAVE_CURRENT_DATA:
      DispatchAsyncSimpleEvent(NS_LITERAL_STRING("waiting"));
      break;
    case nsIDOMHTMLMediaElement::HAVE_FUTURE_DATA:
    case nsIDOMHTMLMediaElement::HAVE_ENOUGH_DATA:
      DispatchAsyncSimpleEvent(NS_LITERAL_STRING("playing"));
      break;
    }
  }

  mPaused = PR_FALSE;
  mAutoplaying = PR_FALSE;

  return NS_OK;
}

PRBool nsHTMLMediaElement::ParseAttribute(PRInt32 aNamespaceID,
                                          nsIAtom* aAttribute,
                                          const nsAString& aValue,
                                          nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (aAttribute == nsGkAtoms::src) {
      static const char* kWhitespace = " \n\r\t\b";
      aResult.SetTo(nsContentUtils::TrimCharsInSet(kWhitespace, aValue));
      return PR_TRUE;
    }
    else if (aAttribute == nsGkAtoms::loopstart
            || aAttribute == nsGkAtoms::loopend
            || aAttribute == nsGkAtoms::start
            || aAttribute == nsGkAtoms::end) {
      return aResult.ParseFloatValue(aValue);
    }
    else if (ParseImageAttribute(aAttribute, aValue, aResult)) {
      return PR_TRUE;
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

nsresult nsHTMLMediaElement::SetAttr(PRInt32 aNameSpaceID, nsIAtom* aName,
                                     nsIAtom* aPrefix, const nsAString& aValue,
                                     PRBool aNotify)
{
  nsresult rv = 
    nsGenericHTMLElement::SetAttr(aNameSpaceID, aName, aPrefix, aValue,
                                    aNotify);
  if (aNotify && aNameSpaceID == kNameSpaceID_None) {
    if (aName == nsGkAtoms::src &&
        IsInDoc() &&
        mNetworkState == nsIDOMHTMLMediaElement::NETWORK_EMPTY) {
      Load();
    }
  }

  return rv;
}

static PRBool IsAutoplayEnabled()
{
  return nsContentUtils::GetBoolPref("media.autoplay.enabled");
}

nsresult nsHTMLMediaElement::BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                                        nsIContent* aBindingParent,
                                        PRBool aCompileEventHandlers)
{
  mIsBindingToTree = PR_TRUE;
  mAutoplayEnabled = IsAutoplayEnabled();
  nsresult rv = nsGenericHTMLElement::BindToTree(aDocument, 
                                                 aParent, 
                                                 aBindingParent, 
                                                 aCompileEventHandlers);
  if (NS_SUCCEEDED(rv) &&
      mIsDoneAddingChildren &&
      mNetworkState == nsIDOMHTMLMediaElement::NETWORK_EMPTY) {
    QueueLoadTask();
  }

  mIsBindingToTree = PR_FALSE;

  return rv;
}

void nsHTMLMediaElement::UnbindFromTree(PRBool aDeep,
                                        PRBool aNullParent)
{
  if (!mPaused && mNetworkState != nsIDOMHTMLMediaElement::NETWORK_EMPTY)
    Pause();

  nsGenericHTMLElement::UnbindFromTree(aDeep, aNullParent);
}

#ifdef MOZ_OGG
// See http://www.rfc-editor.org/rfc/rfc5334.txt for the definitions
// of Ogg media types and codec types
static const char gOggTypes[][16] = {
  "video/ogg",
  "audio/ogg",
  "application/ogg"
};

static const char* gOggCodecs[] = {
  "vorbis",
  "theora",
  nsnull
};

static const char* gOggMaybeCodecs[] = {
  nsnull
}; 

static PRBool IsOggEnabled()
{
  return nsContentUtils::GetBoolPref("media.ogg.enabled");
}

static PRBool IsOggType(const nsACString& aType)
{
  if (!IsOggEnabled())
    return PR_FALSE;
  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gOggTypes); ++i) {
    if (aType.EqualsASCII(gOggTypes[i]))
      return PR_TRUE;
  }
  return PR_FALSE;
}
#endif

#ifdef MOZ_WAVE
// See http://www.rfc-editor.org/rfc/rfc2361.txt for the definitions
// of WAVE media types and codec types. However, the audio/vnd.wave
// MIME type described there is not used.
static const char gWaveTypes[][16] = {
  "audio/x-wav",
  "audio/wav",
  "audio/wave",
  "audio/x-pn-wav"
};

static const char* gWaveCodecs[] = {
  "1", // Microsoft PCM Format
  nsnull
};

static const char* gWaveMaybeCodecs[] = {
  "0", // Microsoft Unknown Wave Format
  nsnull
};

static PRBool IsWaveEnabled()
{
  return nsContentUtils::GetBoolPref("media.wave.enabled");
}

static PRBool IsWaveType(const nsACString& aType)
{
  if (!IsWaveEnabled())
    return PR_FALSE;
  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gWaveTypes); ++i) {
    if (aType.EqualsASCII(gWaveTypes[i]))
      return PR_TRUE;
  }
  return PR_FALSE;
}
#endif

/* static */
PRBool nsHTMLMediaElement::CanHandleMediaType(const char* aMIMEType,
                                              const char*** aCodecList,
                                              const char*** aMaybeCodecList)
{
#ifdef MOZ_OGG
  if (IsOggType(nsDependentCString(aMIMEType))) {
    *aCodecList = gOggCodecs;
    *aMaybeCodecList = gOggMaybeCodecs;
    return PR_TRUE;
  }
#endif
#ifdef MOZ_WAVE
  if (IsWaveType(nsDependentCString(aMIMEType))) {
    *aCodecList = gWaveCodecs;
    *aMaybeCodecList = gWaveMaybeCodecs;
    return PR_TRUE;
  }
#endif
  return PR_FALSE;
}

static PRBool
CodecListContains(const char** aCodecs, const nsAString& aCodec)
{
  for (PRInt32 i = 0; aCodecs[i]; ++i) {
    if (aCodec.EqualsASCII(aCodecs[i]))
      return PR_TRUE;
  }
  return PR_FALSE;
}

enum CanPlayStatus {
  CANPLAY_NO,
  CANPLAY_MAYBE,
  CANPLAY_YES
};

static CanPlayStatus GetCanPlay(const nsAString& aType)
{
  nsContentTypeParser parser(aType);
  nsAutoString mimeType;
  nsresult rv = parser.GetType(mimeType);
  if (NS_FAILED(rv))
    return CANPLAY_NO;

  NS_ConvertUTF16toUTF8 mimeTypeUTF8(mimeType);
  const char** supportedCodecs;
  const char** maybeSupportedCodecs;
  if (!nsHTMLMediaElement::CanHandleMediaType(mimeTypeUTF8.get(),
          &supportedCodecs, &maybeSupportedCodecs))
    return CANPLAY_NO;

  nsAutoString codecs;
  rv = parser.GetParameter("codecs", codecs);
  if (NS_FAILED(rv))
    // Parameter not found or whatever
    return CANPLAY_MAYBE;

  CanPlayStatus result = CANPLAY_YES;
  // See http://www.rfc-editor.org/rfc/rfc4281.txt for the description
  // of the 'codecs' parameter
  nsCommaSeparatedTokenizer tokenizer(codecs);
  PRBool expectMoreTokens = PR_FALSE;
  while (tokenizer.hasMoreTokens()) {
    const nsSubstring& token = tokenizer.nextToken();

    if (CodecListContains(maybeSupportedCodecs, token)) {
      result = CANPLAY_MAYBE;
    } else if (!CodecListContains(supportedCodecs, token)) {
      // Totally unsupported codec
      return CANPLAY_NO;
    }
    expectMoreTokens = tokenizer.lastTokenEndedWithComma();
  }
  if (expectMoreTokens) {
    // Last codec name was empty
    return CANPLAY_NO;
  }
  return result;
}

NS_IMETHODIMP
nsHTMLMediaElement::CanPlayType(const nsAString& aType, nsAString& aResult)
{
  switch (GetCanPlay(aType)) {
  case CANPLAY_NO: aResult.AssignLiteral("no"); break;
  case CANPLAY_YES: aResult.AssignLiteral("probably"); break;
  default:
  case CANPLAY_MAYBE: aResult.AssignLiteral("maybe"); break;
  }
  return NS_OK;
}

/* static */
void nsHTMLMediaElement::InitMediaTypes()
{
  nsresult rv;
  nsCOMPtr<nsICategoryManager> catMan(do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv));
  if (NS_SUCCEEDED(rv)) {
#ifdef MOZ_OGG
    if (IsOggEnabled()) {
      for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gOggTypes); i++) {
        catMan->AddCategoryEntry("Gecko-Content-Viewers", gOggTypes[i],
                                 "@mozilla.org/content/document-loader-factory;1",
                                 PR_FALSE, PR_TRUE, nsnull);
      }
    }
#endif
#ifdef MOZ_WAVE
    if (IsWaveEnabled()) {
      for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gWaveTypes); i++) {
        catMan->AddCategoryEntry("Gecko-Content-Viewers", gWaveTypes[i],
                                 "@mozilla.org/content/document-loader-factory;1",
                                 PR_FALSE, PR_TRUE, nsnull);
      }
    }
#endif
  }
}

/* static */
void nsHTMLMediaElement::ShutdownMediaTypes()
{
  nsresult rv;
  nsCOMPtr<nsICategoryManager> catMan(do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv));
  if (NS_SUCCEEDED(rv)) {
#ifdef MOZ_OGG
    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gOggTypes); i++) {
      catMan->DeleteCategoryEntry("Gecko-Content-Viewers", gOggTypes[i], PR_FALSE);
    }
#endif
#ifdef MOZ_WAVE
    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gWaveTypes); i++) {
      catMan->DeleteCategoryEntry("Gecko-Content-Viewers", gWaveTypes[i], PR_FALSE);
    }
#endif
  }
}

PRBool nsHTMLMediaElement::CreateDecoder(const nsACString& aType)
{
#ifdef MOZ_OGG
  if (IsOggType(aType)) {
    mDecoder = new nsOggDecoder();
    if (mDecoder && !mDecoder->Init(this)) {
      mDecoder = nsnull;
    }
  }
#endif
#ifdef MOZ_WAVE
  if (IsWaveType(aType)) {
    mDecoder = new nsWaveDecoder();
    if (mDecoder && !mDecoder->Init(this)) {
      mDecoder = nsnull;
    }
  }
#endif
  return mDecoder != nsnull;
}

nsresult nsHTMLMediaElement::InitializeDecoderForChannel(nsIChannel *aChannel,
                                                         nsIStreamListener **aListener)
{
  nsCAutoString mimeType;
  aChannel->GetContentType(mimeType);

  if (!CreateDecoder(mimeType))
    return NS_ERROR_FAILURE;

  mNetworkState = nsIDOMHTMLMediaElement::NETWORK_LOADING;

  nsresult rv = mDecoder->Load(nsnull, aChannel, aListener);
  if (NS_FAILED(rv))
    return rv;

  mDecoder->SetVolume(mMuted ? 0.0 : mVolume);

  if (!mPaused) {
    rv = mDecoder->Play();
  }

  return rv;
}

nsresult nsHTMLMediaElement::NewURIFromString(const nsAutoString& aURISpec, nsIURI** aURI)
{
  NS_ENSURE_ARG_POINTER(aURI);

  *aURI = nsnull;

  nsCOMPtr<nsIDocument> doc = GetOwnerDoc();
  if (!doc) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  nsCOMPtr<nsIURI> baseURI = GetBaseURI();
  nsresult rv = nsContentUtils::NewURIWithDocumentCharset(aURI,
                                                          aURISpec,
                                                          doc,
                                                          baseURI);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool equal;
  if (aURISpec.IsEmpty() &&
      doc->GetDocumentURI() &&
      NS_SUCCEEDED(doc->GetDocumentURI()->Equals(*aURI, &equal)) &&
      equal) {
    // It's not possible for a media resource to be embedded in the current
    // document we extracted aURISpec from, so there's no point returning
    // the current document URI just to let the caller attempt and fail to
    // decode it.
    NS_RELEASE(*aURI);
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  return NS_OK;
}

// Implements 'generate the list of potential media resources' as per:
// http://www.whatwg.org/specs/web-apps/current-work/multipage/video.html#generate-the-list-of-potential-media-resources
void nsHTMLMediaElement::GenerateCandidates()
{
  NS_ASSERTION(mCurrentLoad, "Need a nsMediaLoad Object");

  nsCOMPtr<nsIURI> uri;
  nsresult res;
  nsAutoString src;

  // If we have a src attribute on the media element, just use that only.
  if (GetAttr(kNameSpaceID_None, nsGkAtoms::src, src)) {
    res = NewURIFromString(src, getter_AddRefs(uri));
    if (NS_SUCCEEDED(res)) {
      mCurrentLoad->AddCandidate(uri);
    }
    return;
  }

  PRUint32 count = GetChildCount();
  for (PRUint32 i = 0; i < count; ++i) {
    nsIContent* child = GetChildAt(i);
    NS_ASSERTION(child, "GetChildCount lied!");

    nsCOMPtr<nsIContent> source = do_QueryInterface(child);
    
    // Only check source element children.
    if (!source ||
        source->Tag() != nsGkAtoms::source ||
        !source->IsNodeOfType(nsINode::eHTML))
        continue;

    nsAutoString type;

    // Must have src attribute.
    if (!source->GetAttr(kNameSpaceID_None, nsGkAtoms::src, src))
      continue;

    // If we have a type attribyte, it must be valid.
    if (source->GetAttr(kNameSpaceID_None, nsGkAtoms::type, type) &&
        GetCanPlay(type) == CANPLAY_NO)
      continue;
    
    res = NewURIFromString(src, getter_AddRefs(uri));
    if (NS_SUCCEEDED(res)) {
      mCurrentLoad->AddCandidate(uri);
    }
  }
}


nsresult nsHTMLMediaElement::PickMediaElement(nsIURI** aURI)
{
  NS_ENSURE_ARG_POINTER(aURI);

  // Implements:
  // http://www.whatwg.org/specs/web-apps/current-work/#pick-a-media-resource
  nsAutoString src;
  if (GetAttr(kNameSpaceID_None, nsGkAtoms::src, src)) {
    return NewURIFromString(src, aURI);
  }

  // Checking of 'source' elements as per:
  // http://www.whatwg.org/specs/web-apps/current-work/#pick-a-media-resource
  PRUint32 count = GetChildCount();
  for (PRUint32 i = 0; i < count; ++i) {
    nsIContent* child = GetChildAt(i);
    NS_ASSERTION(child, "GetChildCount lied!");

    nsCOMPtr<nsIContent> source = do_QueryInterface(child);
    if (source &&
        source->Tag() == nsGkAtoms::source &&
        source->IsNodeOfType(nsINode::eHTML)) {
      nsAutoString type;
      nsAutoString src;
      if (source->GetAttr(kNameSpaceID_None, nsGkAtoms::src, src)) {
        if (source->GetAttr(kNameSpaceID_None, nsGkAtoms::type, type)) {
          if (GetCanPlay(type) != CANPLAY_NO)
            return NewURIFromString(src, aURI);
        } else if (i + 1 == count) {
          // The last source element is permitted to omit the type
          // attribute, in which case we need to open the URI and examine
          // the channel's MIME type.
          return NewURIFromString(src, aURI);
        }
      }
    }
  }

  return NS_ERROR_DOM_INVALID_STATE_ERR;
}

void nsHTMLMediaElement::MetadataLoaded()
{
  ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_METADATA);
  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("durationchange"));
  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("loadedmetadata"));
}

void nsHTMLMediaElement::FirstFrameLoaded()
{
  ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_CURRENT_DATA);
}

void nsHTMLMediaElement::ResourceLoaded()
{
  mBegun = PR_FALSE;
  mNetworkState = nsIDOMHTMLMediaElement::NETWORK_LOADED;
  ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_ENOUGH_DATA);
  DispatchAsyncProgressEvent(NS_LITERAL_STRING("load"));
}

void nsHTMLMediaElement::NetworkError()
{
  mError = new nsHTMLMediaError(nsIDOMHTMLMediaError::MEDIA_ERR_NETWORK);
  mBegun = PR_FALSE;
  DispatchAsyncProgressEvent(NS_LITERAL_STRING("error"));
  mNetworkState = nsIDOMHTMLMediaElement::NETWORK_EMPTY;
  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("emptied"));
}

void nsHTMLMediaElement::PlaybackEnded()
{
  NS_ASSERTION(mDecoder->IsEnded(), "Decoder fired ended, but not in ended state");
  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("ended"));
}

void nsHTMLMediaElement::SeekStarted()
{
  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("seeking"));
}

void nsHTMLMediaElement::SeekCompleted()
{
  mPlayingBeforeSeek = PR_FALSE;
  DispatchAsyncSimpleEvent(NS_LITERAL_STRING("seeked"));
}

PRBool nsHTMLMediaElement::ShouldCheckAllowOrigin()
{
  return nsContentUtils::GetBoolPref("media.enforce_same_site_origin",
                                     PR_TRUE);
}

// Number of bytes to add to the download size when we're computing
// when the download will finish --- a safety margin in case bandwidth
// or other conditions are worse than expected
static const PRInt32 gDownloadSizeSafetyMargin = 1000000;

void nsHTMLMediaElement::UpdateReadyStateForData(NextFrameStatus aNextFrame)
{
  if (mReadyState < nsIDOMHTMLMediaElement::HAVE_METADATA) {
    NS_ASSERTION(aNextFrame != NEXT_FRAME_AVAILABLE,
                 "How can we have a frame but no metadata?");
    // The arrival of more data can't change us out of this state.
    return;
  }

  if (aNextFrame != NEXT_FRAME_AVAILABLE && !mDecoder->IsEnded()) {
    ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_CURRENT_DATA);
    if (!mWaitingFired && aNextFrame == NEXT_FRAME_UNAVAILABLE_BUFFERING) {
      DispatchAsyncSimpleEvent(NS_LITERAL_STRING("waiting"));
      mWaitingFired = PR_TRUE;
    }
    return;
  }

  // Now see if we should set HAVE_ENOUGH_DATA
  nsMediaDecoder::Statistics stats = mDecoder->GetStatistics();
  if (stats.mTotalBytes < 0 || stats.mTotalBytes == stats.mDownloadPosition) {
    // If it's something we don't know the size of, then we can't
    // make an estimate, so let's just go straight to HAVE_ENOUGH_DATA,
    // since otherwise autoplay elements will never play.
    ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_ENOUGH_DATA);
    return;
  }

  if (stats.mDownloadRateReliable && stats.mPlaybackRateReliable) {
    PRInt64 bytesToDownload = stats.mTotalBytes - stats.mDownloadPosition;
    PRInt64 bytesToPlayback = stats.mTotalBytes - stats.mPlaybackPosition;
    double timeToDownload =
      (bytesToDownload + gDownloadSizeSafetyMargin)/stats.mDownloadRate;
    double timeToPlay = bytesToPlayback/stats.mPlaybackRate;
    if (timeToDownload <= timeToPlay) {
      ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_ENOUGH_DATA);
      return;
    }
  }

  ChangeReadyState(nsIDOMHTMLMediaElement::HAVE_FUTURE_DATA);
}

void nsHTMLMediaElement::ChangeReadyState(nsMediaReadyState aState)
{
  nsMediaReadyState oldState = mReadyState;

  // Handle raising of "waiting" event during seek (see 4.8.10.9)
  if (mPlayingBeforeSeek && oldState < nsIDOMHTMLMediaElement::HAVE_FUTURE_DATA) {
    DispatchAsyncSimpleEvent(NS_LITERAL_STRING("waiting"));
  }
 
  mReadyState = aState;
  if (mNetworkState != nsIDOMHTMLMediaElement::NETWORK_EMPTY) {
    switch (mReadyState) {
    case nsIDOMHTMLMediaElement::HAVE_NOTHING:
      if (oldState != mReadyState) {
        LOG(PR_LOG_DEBUG, ("Ready state changed to HAVE_NOTHING"));
      }
      break;

    case nsIDOMHTMLMediaElement::HAVE_METADATA:
      if (oldState != mReadyState) {
        LOG(PR_LOG_DEBUG, ("Ready state changed to HAVE_METADATA"));
      }
      break;

    case nsIDOMHTMLMediaElement::HAVE_CURRENT_DATA:
      if (oldState != mReadyState) {
        LOG(PR_LOG_DEBUG, ("Ready state changed to HAVE_CURRENT_DATA"));
        mWaitingFired = PR_FALSE;
      }
      if (oldState <= nsIDOMHTMLMediaElement::HAVE_METADATA &&
          !mLoadedFirstFrame) {
        DispatchAsyncSimpleEvent(NS_LITERAL_STRING("loadeddata"));
        mLoadedFirstFrame = PR_TRUE;
      }
      break;
 
    case nsIDOMHTMLMediaElement::HAVE_FUTURE_DATA:
      if (oldState != mReadyState) {
        LOG(PR_LOG_DEBUG, ("Ready state changed to HAVE_FUTURE_DATA"));
      }
      if (oldState <= nsIDOMHTMLMediaElement::HAVE_CURRENT_DATA) {
        DispatchAsyncSimpleEvent(NS_LITERAL_STRING("canplay"));
        if (IsPotentiallyPlaying()) {
          DispatchAsyncSimpleEvent(NS_LITERAL_STRING("playing"));
        }
      }
      break;
 
    case nsIDOMHTMLMediaElement::HAVE_ENOUGH_DATA:
      if (oldState != mReadyState) {
        LOG(PR_LOG_DEBUG, ("Ready state changed to HAVE_ENOUGH_DATA"));
      }
      if (oldState <= nsIDOMHTMLMediaElement::HAVE_CURRENT_DATA) {
        DispatchAsyncSimpleEvent(NS_LITERAL_STRING("canplay"));
      }
      if (mAutoplaying &&
          mPaused &&
          HasAttr(kNameSpaceID_None, nsGkAtoms::autoplay) &&
          mAutoplayEnabled) {
        mPaused = PR_FALSE;
        if (mDecoder) {
          mDecoder->Play();
        }
        DispatchAsyncSimpleEvent(NS_LITERAL_STRING("play"));
      }
      if (oldState <= nsIDOMHTMLMediaElement::HAVE_CURRENT_DATA &&
          IsPotentiallyPlaying()) {
        DispatchAsyncSimpleEvent(NS_LITERAL_STRING("playing"));
      }
      if (oldState <= nsIDOMHTMLMediaElement::HAVE_FUTURE_DATA) {
        DispatchAsyncSimpleEvent(NS_LITERAL_STRING("canplaythrough"));
      }
      break;
    }
  }
}

void nsHTMLMediaElement::Paint(gfxContext* aContext, const gfxRect& aRect) 
{
  if (mDecoder)
    mDecoder->Paint(aContext, aRect);
}

nsresult nsHTMLMediaElement::DispatchSimpleEvent(const nsAString& aName)
{
  return nsContentUtils::DispatchTrustedEvent(GetOwnerDoc(), 
                                              static_cast<nsIContent*>(this), 
                                              aName, 
                                              PR_TRUE, 
                                              PR_TRUE);
}

nsresult nsHTMLMediaElement::DispatchAsyncSimpleEvent(const nsAString& aName)
{
  nsCOMPtr<nsIRunnable> event = new nsAsyncEventRunner(aName, this, PR_FALSE);
  NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL); 
  return NS_OK;                           
}

nsresult nsHTMLMediaElement::DispatchAsyncProgressEvent(const nsAString& aName)
{
  nsCOMPtr<nsIRunnable> event = new nsAsyncEventRunner(aName, this, PR_TRUE);
  NS_DispatchToMainThread(event, NS_DISPATCH_NORMAL); 
  return NS_OK;                           
}

nsresult nsHTMLMediaElement::DispatchProgressEvent(const nsAString& aName)
{
  nsCOMPtr<nsIDOMDocumentEvent> docEvent(do_QueryInterface(GetOwnerDoc()));
  nsCOMPtr<nsIDOMEventTarget> target(do_QueryInterface(static_cast<nsIContent*>(this)));
  NS_ENSURE_TRUE(docEvent && target, NS_ERROR_INVALID_ARG);

  nsCOMPtr<nsIDOMEvent> event;
  nsresult rv = docEvent->CreateEvent(NS_LITERAL_STRING("ProgressEvent"), getter_AddRefs(event));
  NS_ENSURE_SUCCESS(rv, rv);
  
  nsCOMPtr<nsIDOMProgressEvent> progressEvent(do_QueryInterface(event));
  NS_ENSURE_TRUE(progressEvent, NS_ERROR_FAILURE);

  PRInt64 totalBytes = 0;
  PRUint64 downloadPosition = 0;
  if (mDecoder) {
    nsMediaDecoder::Statistics stats = mDecoder->GetStatistics();
    totalBytes = stats.mTotalBytes;
    downloadPosition = stats.mDownloadPosition;
  }
  rv = progressEvent->InitProgressEvent(aName, PR_TRUE, PR_TRUE,
    totalBytes >= 0, downloadPosition, totalBytes);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool dummy;
  return target->DispatchEvent(event, &dummy);  
}

nsresult nsHTMLMediaElement::DoneAddingChildren(PRBool aHaveNotified)
{
  if (!mIsDoneAddingChildren) {
    mIsDoneAddingChildren = PR_TRUE;
  
    if (mNetworkState == nsIDOMHTMLMediaElement::NETWORK_EMPTY) {
      QueueLoadTask();
    }
  }

  return NS_OK;
}

PRBool nsHTMLMediaElement::IsDoneAddingChildren()
{
  return mIsDoneAddingChildren;
}

PRBool nsHTMLMediaElement::IsPotentiallyPlaying() const
{
  // TODO: 
  //   playback has not stopped due to errors, 
  //   and the element has not paused for user interaction
  return 
    !mPaused && 
    (mReadyState == nsIDOMHTMLMediaElement::HAVE_ENOUGH_DATA ||
    mReadyState == nsIDOMHTMLMediaElement::HAVE_FUTURE_DATA) &&
    !IsPlaybackEnded();
}

PRBool nsHTMLMediaElement::IsPlaybackEnded() const
{
  // TODO:
  //   the current playback position is equal to the effective end of the media resource, 
  //   and the currentLoop attribute is equal to playCount-1. 
  //   See bug 449157.
  return mNetworkState >= nsIDOMHTMLMediaElement::HAVE_METADATA &&
    mDecoder ? mDecoder->IsEnded() : PR_FALSE;
}

nsIPrincipal* nsHTMLMediaElement::GetCurrentPrincipal()
{
  if (!mDecoder)
    return nsnull;

  return mDecoder->GetCurrentPrincipal();
}

void nsHTMLMediaElement::UpdateMediaSize(nsIntSize size)
{
  mMediaSize = size;
}

void nsHTMLMediaElement::DestroyContent()
{
  if (mDecoder) {
    mDecoder->Shutdown();
    mDecoder = nsnull;
  }
  if (mChannel) {
    mChannel->Cancel(NS_BINDING_ABORTED);
    mChannel = nsnull;
  }
  nsGenericHTMLElement::DestroyContent();
}

void nsHTMLMediaElement::Freeze()
{
  mPausedBeforeFreeze = mPaused;
  if (!mPaused) {
    Pause();
  }
  if (mDecoder) {
    mDecoder->Suspend();
  }
}

void nsHTMLMediaElement::Thaw()
{
  if (!mPausedBeforeFreeze) {
    Play();
  }

  if (mDecoder) {
    mDecoder->Resume();
  }
}

PRBool
nsHTMLMediaElement::IsNodeOfType(PRUint32 aFlags) const
{
  return !(aFlags & ~(eCONTENT | eELEMENT | eHTML | eMEDIA));
}

void nsHTMLMediaElement::NotifyAddedSource()
{
  // Binding a source element to a media element could trigger a new load.
  // See HTML spec, '4.8.9 The source element' for conditions:
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/video.html#the-source-element
  // Note: we must not start a load if we're in nsHTMLMediaElement::BindToTree(),
  // that will trigger a load when it completes.
  PRBool shouldLoad = IsInDoc() &&
                      !mIsBindingToTree &&
                      mIsDoneAddingChildren &&
                      mNetworkState == nsIDOMHTMLMediaElement::NETWORK_EMPTY;
  if (shouldLoad) {
    QueueLoadTask();
  }
}

/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/BeforeAfterKeyboardEvent.h"
#include "mozilla/TextEvents.h"
#include "prtime.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Key", args);
#else
#define LOG(args...) printf(args);
#endif

namespace mozilla {
namespace dom {

BeforeAfterKeyboardEvent::BeforeAfterKeyboardEvent(
                                       EventTarget* aOwner,
                                       nsPresContext* aPresContext,
                                       InternalBeforeAfterKeyboardEvent* aEvent)
  : KeyboardEvent(aOwner, aPresContext,
                  aEvent ? aEvent :
                           new InternalBeforeAfterKeyboardEvent(false, 0,
                                                                nullptr))
{
  MOZ_ASSERT(mEvent->mClass == eBeforeAfterKeyboardEventClass,
             "event type mismatch eBeforeAfterKeyboardEventClass");

  if (!aEvent) {
    mEventIsInternal = true;
    mEvent->time = PR_Now();
  }
}

// static
already_AddRefed<BeforeAfterKeyboardEvent>
BeforeAfterKeyboardEvent::Constructor(
                            EventTarget* aOwner,
                            const nsAString& aType,
                            const BeforeAfterKeyboardEventInit& aParam)
{
  nsRefPtr<BeforeAfterKeyboardEvent> event =
    new BeforeAfterKeyboardEvent(aOwner, nullptr, nullptr);
  ErrorResult rv;
  event->InitWithKeyboardEventInit(aOwner, aType, aParam, rv);
  NS_WARN_IF(rv.Failed());

  event->mEvent->AsBeforeAfterKeyboardEvent()->mEmbeddedCancelled =
    aParam.mEmbeddedCancelled;

  return event.forget();
}

// static
already_AddRefed<BeforeAfterKeyboardEvent>
BeforeAfterKeyboardEvent::Constructor(
                            const GlobalObject& aGlobal,
                            const nsAString& aType,
                            const BeforeAfterKeyboardEventInit& aParam,
                            ErrorResult& aRv)
{
  nsCOMPtr<EventTarget> owner = do_QueryInterface(aGlobal.GetAsSupports());
  return Constructor(owner, aType, aParam);
}

Nullable<bool>
BeforeAfterKeyboardEvent::GetEmbeddedCancelled()
{
  nsAutoString type;
  GetType(type);
  LOG("[%s], GetType(): %s", __FUNCTION__, NS_ConvertUTF16toUTF8(type).get());
  if (type.EqualsLiteral("mozbrowserafterkeydown") ||
      type.EqualsLiteral("mozbrowserafterkeyup")) {
    if (!mEvent->AsBeforeAfterKeyboardEvent()->mEmbeddedCancelled.IsNull()) {
      LOG("[%s] mEvent->mEmbeddedCancelled: %d", __FUNCTION__, mEvent->AsBeforeAfterKeyboardEvent()->mEmbeddedCancelled.Value());
    }
    return mEvent->AsBeforeAfterKeyboardEvent()->mEmbeddedCancelled;
  }
  return Nullable<bool>();
}

} // namespace dom
} // namespace mozilla

using namespace mozilla;
using namespace mozilla::dom;

nsresult
NS_NewDOMBeforeAfterKeyboardEvent(nsIDOMEvent** aInstancePtrResult,
                                  EventTarget* aOwner,
                                  nsPresContext* aPresContext,
                                  InternalBeforeAfterKeyboardEvent* aEvent)
{
  LOG("NS_NewDOMBeforeAfterKeyboardEvent");
  BeforeAfterKeyboardEvent* it =
    new BeforeAfterKeyboardEvent(aOwner, aPresContext, aEvent);

  NS_ADDREF(it);
  *aInstancePtrResult = static_cast<Event*>(it);
  return NS_OK;
}

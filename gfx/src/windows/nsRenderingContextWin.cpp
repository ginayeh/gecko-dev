/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include "nsRenderingContextWin.h"
#include <math.h>

class GraphicsState
{
public:
  GraphicsState();
  GraphicsState(GraphicsState &aState);
  ~GraphicsState();

  GraphicsState   *mNext;
  nsTransform2D   mMatrix;
  nsRect          mLocalClip;
  nsRect          mGlobalClip;
  HRGN            mClipRegion;
  nscolor         mBrushColor;
  HBRUSH          mSolidBrush;
  nsIFontMetrics  *mFontMetrics;
  HFONT           mFont;
  nscolor         mPenColor;
  HPEN            mSolidPen;
};

GraphicsState :: GraphicsState()
{
  mNext = nsnull;
  mMatrix.SetToIdentity();  
  mLocalClip.x = mLocalClip.y = mLocalClip.width = mLocalClip.height = 0;
  mGlobalClip = mLocalClip;
  mClipRegion = NULL;
  mBrushColor = NS_RGB(0, 0, 0);
  mSolidBrush = NULL;
  mFontMetrics = nsnull;
  mFont = NULL;
  mPenColor = NS_RGB(0, 0, 0);
  mSolidPen = NULL;
}

GraphicsState :: GraphicsState(GraphicsState &aState) :
                               mMatrix(&aState.mMatrix),
                               mLocalClip(aState.mLocalClip),
                               mGlobalClip(aState.mGlobalClip)
{
  mNext = &aState;
  mClipRegion = NULL;
  mBrushColor = aState.mBrushColor;
  mSolidBrush = NULL;
  mFontMetrics = nsnull;
  mFont = NULL;
  mPenColor = aState.mPenColor;
  mSolidPen = NULL;
}

GraphicsState :: ~GraphicsState()
{
  if (NULL != mClipRegion)
  {
    ::DeleteObject(mClipRegion);
    mClipRegion = NULL;
  }

  if (NULL != mSolidBrush)
  {
    ::DeleteObject(mSolidBrush);
    mSolidBrush = NULL;
  }

  //don't delete this because it lives in the font metrics
  mFont = NULL;

  if (NULL != mSolidPen)
  {
    ::DeleteObject(mSolidPen);
    mSolidPen = NULL;
  }
}

static NS_DEFINE_IID(kRenderingContextIID, NS_IRENDERING_CONTEXT_IID);

nsRenderingContextWin :: nsRenderingContextWin()
{
  NS_INIT_REFCNT();

  //mFont = nsnull;
  mDC = NULL;
  mMainDC = NULL;
  mDCOwner = nsnull;
  mFontMetrics = nsnull;
  mFontCache = nsnull;
  mOrigSolidBrush = NULL;
  mBlackBrush = NULL;
  mOrigFont = NULL;
  mDefFont = NULL;
  mOrigSolidPen = NULL;
  mBlackPen = NULL;
#ifdef NS_DEBUG
  mInitialized = PR_FALSE;
#endif

  mStateCache = new nsVoidArray();

  //create an initial GraphicsState

  PushState();

  mP2T = 1.0f;
}

nsRenderingContextWin :: ~nsRenderingContextWin()
{
  NS_IF_RELEASE(mContext);
  NS_IF_RELEASE(mFontMetrics);
  NS_IF_RELEASE(mFontCache);

  //destroy the initial GraphicsState

  PopState();

  //cleanup the DC so that we can just destroy objects
  //in the graphics state without worrying that we are
  //ruining the dc

  if (NULL != mDC)
  {
    if (NULL != mOrigSolidBrush)
    {
      ::SelectObject(mDC, mOrigSolidBrush);
      mOrigSolidBrush = NULL;
    }

    if (NULL != mBlackBrush)
    {
      ::DeleteObject(mBlackBrush);
      mBlackBrush = NULL;
    }

    if (NULL != mOrigFont)
    {
      ::SelectObject(mDC, mOrigFont);
      mOrigFont = NULL;
    }

    if (NULL != mDefFont)
    {
      ::DeleteObject(mDefFont);
      mDefFont = NULL;
    }

    if (NULL != mOrigSolidPen)
    {
      ::SelectObject(mDC, mOrigSolidPen);
      mOrigSolidPen = NULL;
    }

    if (NULL != mBlackPen)
    {
      ::DeleteObject(mBlackPen);
      mBlackPen = NULL;
    }
  }

  if (nsnull != mStateCache)
  {
    PRInt32 cnt = mStateCache->Count();

    while (--cnt >= 0)
    {
      GraphicsState *state = (GraphicsState *)mStateCache->ElementAt(cnt);
      mStateCache->RemoveElementAt(cnt);

      if (nsnull != state)
        delete state;
    }

    delete mStateCache;
    mStateCache = nsnull;
  }

  if (nsnull != mDCOwner)
  {
    //first try to get rid of a DC originally associated with the window
    //but pushed over to a dest DC for offscreen rendering. if there is no
    //rolled over DC, then the mDC is the one associated with the window.

    if (nsnull != mMainDC)
      ReleaseDC((HWND)mDCOwner->GetNativeData(NS_NATIVE_WINDOW), mMainDC);
    else if (nsnull != mDC)
      ReleaseDC((HWND)mDCOwner->GetNativeData(NS_NATIVE_WINDOW), mDC);
  }

  NS_IF_RELEASE(mDCOwner);

  mTMatrix = nsnull;
  mDC = NULL;
  mMainDC = NULL;
}

NS_IMPL_QUERY_INTERFACE(nsRenderingContextWin, kRenderingContextIID)
NS_IMPL_ADDREF(nsRenderingContextWin)
NS_IMPL_RELEASE(nsRenderingContextWin)

nsresult nsRenderingContextWin :: Init(nsIDeviceContext* aContext,
                                       nsIWidget *aWindow)
{
  NS_PRECONDITION(PR_FALSE == mInitialized, "double init");

  mContext = aContext;
  NS_IF_ADDREF(mContext);

  mDC = (HWND)aWindow->GetNativeData(NS_NATIVE_GRAPHIC);
  mDCOwner = aWindow;

  if (mDCOwner)
    NS_ADDREF(mDCOwner);

  return CommonInit();
}

nsresult nsRenderingContextWin :: Init(nsIDeviceContext* aContext,
                                       nsDrawingSurface aSurface)
{
  NS_PRECONDITION(PR_FALSE == mInitialized, "double init");

  mContext = aContext;
  NS_IF_ADDREF(mContext);

  mDC = (HDC)aSurface;
  mDCOwner = nsnull;

  return CommonInit();
}

nsresult nsRenderingContextWin :: CommonInit(void)
{
	mTMatrix->AddScale(mContext->GetAppUnitsToDevUnits(),
                     mContext->GetAppUnitsToDevUnits());
  mP2T = mContext->GetDevUnitsToAppUnits();
  mFontCache = mContext->GetFontCache();

#ifdef NS_DEBUG
  mInitialized = PR_TRUE;
#endif

  mBlackBrush = ::CreateSolidBrush(RGB(0, 0, 0));
  mOrigSolidBrush = ::SelectObject(mDC, mBlackBrush);

  mDefFont = ::CreateFont(12, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
                          ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, FF_ROMAN | VARIABLE_PITCH, "Times New Roman");
  mOrigFont = ::SelectObject(mDC, mDefFont);

  mBlackPen = ::CreatePen(PS_SOLID, 0, RGB(0, 0, 0));
  mOrigSolidPen = ::SelectObject(mDC, mBlackPen);

  return NS_OK;
}

nsresult nsRenderingContextWin :: SelectOffScreenDrawingSurface(nsDrawingSurface aSurface)
{
  mMainDC = mDC;
  mDC = (HDC)aSurface;

  return NS_OK;
}

void nsRenderingContextWin :: Reset()
{
}

nsIDeviceContext * nsRenderingContextWin :: GetDeviceContext(void)
{
  NS_IF_ADDREF(mContext);
  return mContext;
}

void nsRenderingContextWin :: PushState()
{
  PRInt32 cnt = mStateCache->Count();

  if (cnt == 0)
  {
    if (nsnull == mStates)
      mStates = new GraphicsState();
    else
      mStates = new GraphicsState(*mStates);
  }
  else
  {
    GraphicsState *state = (GraphicsState *)mStateCache->ElementAt(cnt - 1);
    mStateCache->RemoveElementAt(cnt - 1);

    state->mNext = mStates;

    //clone state info

    state->mMatrix = mStates->mMatrix;
    state->mLocalClip = mStates->mLocalClip;
    state->mGlobalClip = mStates->mGlobalClip;
    state->mClipRegion = nsnull;
    state->mBrushColor = mStates->mBrushColor;
    state->mSolidBrush = nsnull;
    state->mFontMetrics = mStates->mFontMetrics;
    state->mFont = nsnull;
    state->mPenColor = mStates->mPenColor;
    state->mSolidPen = nsnull;

    mStates = state;
  }

  mTMatrix = &mStates->mMatrix;
}

void nsRenderingContextWin :: PopState()
{
  if (nsnull == mStates)
  {
    NS_ASSERTION(!(nsnull == mStates), "state underflow");
  }
  else
  {
    GraphicsState *oldstate = mStates;

    mStates = mStates->mNext;

    mStateCache->AppendElement(oldstate);

    if (nsnull != mStates)
    {
      mTMatrix = &mStates->mMatrix;

      GraphicsState *pstate;

      if (NULL != oldstate->mClipRegion)
      {
        pstate = mStates;

        //the clip rect has changed from state to state, so
        //install the previous clip rect

        while ((nsnull != pstate) && (NULL == pstate->mClipRegion))
          pstate = pstate->mNext;

        if ((nsnull != pstate) && (pstate->mGlobalClip != oldstate->mGlobalClip))
          ::SelectClipRgn(mDC, pstate->mClipRegion);
        else
          ::SelectClipRgn(mDC, NULL);

        //kill the clip region we are popping off the stack

        ::DeleteObject(oldstate->mClipRegion);
        oldstate->mClipRegion = NULL;
      }

      if (NULL != oldstate->mSolidBrush)
      {
        pstate = mStates;

        //if the solid brushes are different between the states,
        //select the previous solid brush

        while ((nsnull != pstate) && (NULL == pstate->mSolidBrush))
          pstate = pstate->mNext;

        if (nsnull != pstate)
        {
          if (oldstate->mSolidBrush != pstate->mSolidBrush)
            ::SelectObject(mDC, pstate->mSolidBrush);
        }
        else
          ::SelectObject(mDC, mBlackBrush);

        //kill the solid brush we are popping off the stack

        if (oldstate->mSolidBrush != mBlackBrush)
        {
          if (((nsnull != pstate) && (oldstate->mSolidBrush != pstate->mSolidBrush)) ||
              (nsnull == pstate))
            ::DeleteObject(oldstate->mSolidBrush);
        }

        oldstate->mSolidBrush = NULL;
      }

      if (NULL != oldstate->mFont)
      {
        pstate = mStates;

        //if the fonts are different between the states,
        //select the previous font

        while ((nsnull != pstate) && (NULL == pstate->mFont))
          pstate = pstate->mNext;

        if (nsnull != pstate)
        {
          if (oldstate->mFont != pstate->mFont)
            ::SelectObject(mDC, pstate->mFont);
        }
        else
          ::SelectObject(mDC, mDefFont);

        //don't delete the font because it lives in the font metrics
        oldstate->mFont = NULL;
      }

      if (NULL != oldstate->mSolidPen)
      {
        pstate = mStates;

        //if the solid pens are different between the states,
        //select the previous solid pen

        while ((nsnull != pstate) && (NULL == pstate->mSolidPen))
          pstate = pstate->mNext;

        if (nsnull != pstate)
        {
          if (oldstate->mSolidPen != pstate->mSolidPen)
            ::SelectObject(mDC, pstate->mSolidPen);
        }
        else
          ::SelectObject(mDC, mBlackPen);

        //kill the solid brush we are popping off the stack

        if (oldstate->mSolidPen != mBlackPen)
        {
          if (((nsnull != pstate) && (oldstate->mSolidPen != pstate->mSolidPen)) ||
              (nsnull == pstate))
            ::DeleteObject(oldstate->mSolidPen);
        }

        oldstate->mSolidPen = NULL;
      }
    }
    else
      mTMatrix = nsnull;
  }
}

PRBool nsRenderingContextWin :: IsVisibleRect(const nsRect& aRect)
{
  return PR_TRUE;
}

void nsRenderingContextWin :: SetClipRect(const nsRect& aRect, PRBool aIntersect)
{
  nsRect  trect = aRect;

  mStates->mLocalClip = aRect;

	mTMatrix->TransformCoord(&trect.x, &trect.y,
                           &trect.width, &trect.height);

  //should we combine the new rect with the previous?

  if (aIntersect == PR_TRUE)
  {
    if (PR_FALSE == mStates->mGlobalClip.IntersectRect(mStates->mGlobalClip, trect))
    {
      mStates->mGlobalClip.x = mStates->mGlobalClip.y = mStates->mGlobalClip.width = mStates->mGlobalClip.height = 0;
    }  
  }
  else
    mStates->mGlobalClip = trect;

  if (NULL != mStates->mClipRegion)
    ::DeleteObject(mStates->mClipRegion);

  mStates->mClipRegion = ::CreateRectRgn(mStates->mGlobalClip.x,
                                         mStates->mGlobalClip.y,
                                         mStates->mGlobalClip.XMost(),
                                         mStates->mGlobalClip.YMost());
  ::SelectClipRgn(mDC, mStates->mClipRegion);
}

const nsRect& nsRenderingContextWin :: GetClipRect()
{
  return mStates->mLocalClip;
}

void nsRenderingContextWin :: SetColor(nscolor aColor)
{
  mCurrentColor = aColor;
  mColor = RGB(NS_GET_R(aColor), NS_GET_G(aColor), NS_GET_B(aColor));
}

nscolor nsRenderingContextWin :: GetColor() const
{
  return mCurrentColor;
}

void nsRenderingContextWin :: SetFont(const nsFont& aFont)
{
  NS_IF_RELEASE(mFontMetrics);
  mFontMetrics = mFontCache->GetMetricsFor(aFont);
}

const nsFont& nsRenderingContextWin :: GetFont()
{
  return mFontMetrics->GetFont();
}

nsIFontMetrics* nsRenderingContextWin :: GetFontMetrics()
{
  return mFontMetrics;
}

// add the passed in translation to the current translation
void nsRenderingContextWin :: Translate(nscoord aX, nscoord aY)
{
	mTMatrix->AddTranslation((float)aX,(float)aY);
}

// add the passed in scale to the current scale
void nsRenderingContextWin :: Scale(float aSx, float aSy)
{
	mTMatrix->AddScale(aSx, aSy);
}

nsTransform2D * nsRenderingContextWin :: GetCurrentTransform()
{
  return mTMatrix;
}

nsDrawingSurface nsRenderingContextWin :: CreateDrawingSurface(nsRect *aBounds)
{
  HDC hDC = ::CreateCompatibleDC(mDC);

  if (nsnull != aBounds)
  {
    HBITMAP hBits = ::CreateCompatibleBitmap(mDC, aBounds->width, aBounds->height);
    ::SelectObject(hDC, hBits);
  }
  else
  {
    HBITMAP hBits = ::CreateCompatibleBitmap(mDC, 2, 2);
    ::SelectObject(hDC, hBits);
  }

  return hDC;
}

void nsRenderingContextWin :: DestroyDrawingSurface(nsDrawingSurface aDS)
{
  HDC hDC = (HDC)aDS;

  HBITMAP hTempBits = ::CreateCompatibleBitmap(hDC, 2, 2);
  HBITMAP hBits = ::SelectObject(hDC, hTempBits);

  if (nsnull != hBits)
    ::DeleteObject(hBits);

  ::DeleteObject(hTempBits);
  ::DeleteDC(hDC);
}

void nsRenderingContextWin :: DrawLine(nscoord aX0, nscoord aY0, nscoord aX1, nscoord aY1)
{
	mTMatrix->TransformCoord(&aX0,&aY0);
	mTMatrix->TransformCoord(&aX1,&aY1);

  SetupSolidPen();

  ::MoveToEx(mDC, (int)(aX0), (int)(aY0), NULL);
  ::LineTo(mDC, (int)(aX1), (int)(aY1));
}

void nsRenderingContextWin :: DrawRect(const nsRect& aRect)
{
  RECT nr;
	nsRect	tr;

	tr = aRect;
	mTMatrix->TransformCoord(&tr.x,&tr.y,&tr.width,&tr.height);
	nr.left = tr.x;
	nr.top = tr.y;
	nr.right = tr.x+tr.width;
	nr.bottom = tr.y+tr.height;

  ::FrameRect(mDC, &nr, SetupSolidBrush());
}

void nsRenderingContextWin :: DrawRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  RECT nr;

	mTMatrix->TransformCoord(&aX,&aY,&aWidth,&aHeight);
	nr.left = aX;
	nr.top = aY;
	nr.right = aX+aWidth;
	nr.bottom = aY+aHeight;

  ::FrameRect(mDC, &nr, SetupSolidBrush());
}

void nsRenderingContextWin :: FillRect(const nsRect& aRect)
{
  RECT nr;
	nsRect	tr;

	tr = aRect;
	mTMatrix->TransformCoord(&tr.x,&tr.y,&tr.width,&tr.height);
	nr.left = tr.x;
	nr.top = tr.y;
	nr.right = tr.x+tr.width;
	nr.bottom = tr.y+tr.height;

  ::FillRect(mDC, &nr, SetupSolidBrush());
}

void nsRenderingContextWin :: FillRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  RECT nr;
	nsRect	tr;

	mTMatrix->TransformCoord(&aX,&aY,&aWidth,&aHeight);
	nr.left = aX;
	nr.top = aY;
	nr.right = aX+aWidth;
	nr.bottom = aY+aHeight;

  ::FillRect(mDC, &nr, SetupSolidBrush());
}

void nsRenderingContextWin::DrawPolygon(nsPoint aPoints[], PRInt32 aNumPoints)
{
  // First transform nsPoint's into POINT's; perform coordinate space
  // transformation at the same time
  POINT pts[20];
  POINT* pp0 = pts;

  if (aNumPoints > 20)
    pp0 = new POINT[aNumPoints];

  POINT* pp = pp0;
  const nsPoint* np = &aPoints[0];

	for (PRInt32 i = 0; i < aNumPoints; i++, pp++, np++)
  {
		pp->x = np->x;
		pp->y = np->y;
		mTMatrix->TransformCoord((int*)&pp->x,(int*)&pp->y);
	}

  // Outline the polygon
  int pfm = ::GetPolyFillMode(mDC);
  ::SetPolyFillMode(mDC, WINDING);
  LOGBRUSH lb;
  lb.lbStyle = BS_NULL;
  lb.lbColor = 0;
  lb.lbHatch = 0;
  HBRUSH brush = ::CreateBrushIndirect(&lb);
  SetupSolidPen();
  HBRUSH oldBrush = ::SelectObject(mDC, brush);
  ::Polygon(mDC, pp0, int(aNumPoints));
  ::SelectObject(mDC, oldBrush);
  ::DeleteObject(brush);
  ::SetPolyFillMode(mDC, pfm);

  // Release temporary storage if necessary
  if (pp0 != pts)
    delete pp0;
}

void nsRenderingContextWin::FillPolygon(nsPoint aPoints[], PRInt32 aNumPoints)
{
  // First transform nsPoint's into POINT's; perform coordinate space
  // transformation at the same time

  POINT pts[20];
  POINT* pp0 = pts;

  if (aNumPoints > 20)
    pp0 = new POINT[aNumPoints];

  POINT* pp = pp0;
  const nsPoint* np = &aPoints[0];

	for (PRInt32 i = 0; i < aNumPoints; i++, pp++, np++)
	{
		pp->x = np->x;
		pp->y = np->y;
		mTMatrix->TransformCoord((int*)&pp->x,(int*)&pp->y);
	}

  // Fill the polygon
  int pfm = ::GetPolyFillMode(mDC);
  ::SetPolyFillMode(mDC, WINDING);
  SetupSolidBrush();
  HPEN pen = ::CreatePen(PS_NULL, 0, 0);
  HPEN oldPen = ::SelectObject(mDC, pen);
  ::Polygon(mDC, pp0, int(aNumPoints));
  ::SelectObject(mDC, oldPen);
  ::DeleteObject(pen);
  ::SetPolyFillMode(mDC, pfm);

  // Release temporary storage if necessary
  if (pp0 != pts)
    delete pp0;
}

void nsRenderingContextWin :: DrawEllipse(const nsRect& aRect)
{
  DrawEllipse(aRect.x, aRect.y, aRect.width, aRect.height);
}

void nsRenderingContextWin :: DrawEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  mTMatrix->TransformCoord(&aX, &aY, &aWidth, &aHeight);

  SetupSolidPen();
  HBRUSH oldBrush = ::SelectObject(mDC, ::GetStockObject(NULL_BRUSH));
  
  ::Ellipse(mDC, aX, aY, aX + aWidth, aY + aHeight);
  
  ::SelectObject(mDC, oldBrush);
}

void nsRenderingContextWin :: FillEllipse(const nsRect& aRect)
{
  FillEllipse(aRect.x, aRect.y, aRect.width, aRect.height);
}

void nsRenderingContextWin :: FillEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  mTMatrix->TransformCoord(&aX, &aY, &aWidth, &aHeight);

  SetupSolidPen();
  SetupSolidBrush();
  
  ::Ellipse(mDC, aX, aY, aX + aWidth, aY + aHeight);
}

void nsRenderingContextWin :: DrawArc(const nsRect& aRect,
                                 float aStartAngle, float aEndAngle)
{
  this->DrawArc(aRect.x,aRect.y,aRect.width,aRect.height,aStartAngle,aEndAngle);
}

void nsRenderingContextWin :: DrawArc(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight,
                                 float aStartAngle, float aEndAngle)
{
  PRInt32 quad1, quad2, sx, sy, ex, ey, cx, cy;
  float   anglerad, distance;

  mTMatrix->TransformCoord(&aX, &aY, &aWidth, &aHeight);

  SetupSolidPen();
  SetupSolidBrush();

  // figure out the the coordinates of the arc from the angle
  distance = (float)sqrt((float)(aWidth * aWidth + aHeight * aHeight));
  cx = aX + aWidth / 2;
  cy = aY + aHeight / 2;

  anglerad = (float)(aStartAngle / (180.0 / 3.14159265358979323846));
  quad1 = (PRInt32)(aStartAngle / 90.0);
  sx = (PRInt32)(distance * cos(anglerad) + cx);
  sy = (PRInt32)(cy - distance * sin(anglerad));

  anglerad = (float)(aEndAngle / (180.0 / 3.14159265358979323846));
  quad2 = (PRInt32)(aEndAngle / 90.0);
  ex = (PRInt32)(distance * cos(anglerad) + cx);
  ey = (PRInt32)(cy - distance * sin(anglerad));

  // this just makes it consitent, on windows 95 arc will always draw CC, nt this sets direction
  ::SetArcDirection(mDC, AD_COUNTERCLOCKWISE);

  ::Arc(mDC, aX, aY, aX + aWidth, aY + aHeight, sx, sy, ex, ey); 
}

void nsRenderingContextWin :: FillArc(const nsRect& aRect,
                                 float aStartAngle, float aEndAngle)
{
  this->FillArc(aRect.x, aRect.y, aRect.width, aRect.height, aStartAngle, aEndAngle);
}

void nsRenderingContextWin :: FillArc(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight,
                                 float aStartAngle, float aEndAngle)
{
  PRInt32 quad1, quad2, sx, sy, ex, ey, cx, cy;
  float   anglerad, distance;

  mTMatrix->TransformCoord(&aX, &aY, &aWidth, &aHeight);

  SetupSolidPen();
  SetupSolidBrush();

  // figure out the the coordinates of the arc from the angle
  distance = (float)sqrt((float)(aWidth * aWidth + aHeight * aHeight));
  cx = aX + aWidth / 2;
  cy = aY + aHeight / 2;

  anglerad = (float)(aStartAngle / (180.0 / 3.14159265358979323846));
  quad1 = (PRInt32)(aStartAngle / 90.0);
  sx = (PRInt32)(distance * cos(anglerad) + cx);
  sy = (PRInt32)(cy - distance * sin(anglerad));

  anglerad = (float)(aEndAngle / (180.0 / 3.14159265358979323846));
  quad2 = (PRInt32)(aEndAngle / 90.0);
  ex = (PRInt32)(distance * cos(anglerad) + cx);
  ey = (PRInt32)(cy - distance * sin(anglerad));

  // this just makes it consitent, on windows 95 arc will always draw CC, nt this sets direction
  ::SetArcDirection(mDC, AD_COUNTERCLOCKWISE);

  ::Pie(mDC, aX, aY, aX + aWidth, aY + aHeight, sx, sy, ex, ey); 
}

void nsRenderingContextWin :: DrawString(const char *aString, PRUint32 aLength,
                                    nscoord aX, nscoord aY,
                                    nscoord aWidth)
{
  int oldBkMode = ::SetBkMode(mDC, TRANSPARENT);
	int	x,y;

  SetupFont();

  COLORREF oldColor = ::SetTextColor(mDC, mColor);
	x = aX;
	y = aY;
	mTMatrix->TransformCoord(&x,&y);
  ::TextOut(mDC,x,y,aString,aLength);

  if (mFontMetrics->GetFont().decorations & NS_FONT_DECORATION_OVERLINE)
    DrawLine(aX, aY, aX + aWidth, aY);

  ::SetBkMode(mDC, oldBkMode);
  ::SetTextColor(mDC, oldColor);
}

void nsRenderingContextWin :: DrawString(const PRUnichar *aString, PRUint32 aLength,
                                         nscoord aX, nscoord aY, nscoord aWidth)
{
	int		x,y;
  int oldBkMode = ::SetBkMode(mDC, TRANSPARENT);

  SetupFont();

	COLORREF oldColor = ::SetTextColor(mDC, mColor);
	x = aX;
	y = aY;
	mTMatrix->TransformCoord(&x,&y);
  ::TextOutW(mDC,x,y,aString,aLength);

  if (mFontMetrics->GetFont().decorations & NS_FONT_DECORATION_OVERLINE)
    DrawLine(aX, aY, aX + aWidth, aY);

  ::SetBkMode(mDC, oldBkMode);
  ::SetTextColor(mDC, oldColor);
}

void nsRenderingContextWin :: DrawString(const nsString& aString,
                                         nscoord aX, nscoord aY, nscoord aWidth)
{
  DrawString(aString.GetUnicode(), aString.Length(), aX, aY, aWidth);
}

void nsRenderingContextWin :: DrawImage(nsIImage *aImage, nscoord aX, nscoord aY)
{
  NS_PRECONDITION(PR_TRUE == mInitialized, "!initialized");

  nscoord width, height;

  width = NS_TO_INT_ROUND(mP2T * aImage->GetWidth());
  height = NS_TO_INT_ROUND(mP2T * aImage->GetHeight());

  this->DrawImage(aImage, aX, aY, width, height);
}

void nsRenderingContextWin :: DrawImage(nsIImage *aImage, nscoord aX, nscoord aY,
                                        nscoord aWidth, nscoord aHeight) 
{
  nsRect  tr;

  tr.x = aX;
  tr.y = aY;
  tr.width = aWidth;
  tr.height = aHeight;

  this->DrawImage(aImage, tr);
}

void nsRenderingContextWin :: DrawImage(nsIImage *aImage, const nsRect& aSRect, const nsRect& aDRect)
{
  nsRect	sr,dr;

	sr = aSRect;
	mTMatrix->TransformCoord(&sr.x, &sr.y, &sr.width, &sr.height);

  dr = aDRect;
	mTMatrix->TransformCoord(&dr.x, &dr.y, &dr.width, &dr.height);

  ((nsImageWin *)aImage)->Draw(*this, mDC, sr.x, sr.y, sr.width, sr.height, dr.x, dr.y, dr.width, dr.height);
}

void nsRenderingContextWin :: DrawImage(nsIImage *aImage, const nsRect& aRect)
{
  nsRect	tr;

	tr = aRect;
	mTMatrix->TransformCoord(&tr.x, &tr.y, &tr.width, &tr.height);

  ((nsImageWin *)aImage)->Draw(*this, mDC, tr.x, tr.y, tr.width, tr.height);
}

nsresult nsRenderingContextWin :: CopyOffScreenBits(nsRect &aBounds)
{
  if ((nsnull != mDC) && (nsnull != mMainDC))
  {
    GraphicsState *pstate = mStates;

    //look for a cliprect somewhere in the stack...

    while ((nsnull != pstate) && (NULL == pstate->mClipRegion))
      pstate = pstate->mNext;

    if (nsnull != pstate)
      ::SelectClipRgn(mMainDC, pstate->mClipRegion);
    else
      ::SelectClipRgn(mMainDC, NULL);

    ::BitBlt(mMainDC, 0, 0, aBounds.width, aBounds.height, mDC, 0, 0, SRCCOPY);
  }
  else
    NS_ASSERTION(0, "attempt to blit with bad DCs");

  return NS_OK;
}

HBRUSH nsRenderingContextWin :: SetupSolidBrush(void)
{
  if (mCurrentColor != mStates->mBrushColor)
  {
    HBRUSH  tbrush = ::CreateSolidBrush(mColor);
    HBRUSH  obrush = ::SelectObject(mDC, tbrush);

    if ((NULL != obrush) && (NULL != mStates->mSolidBrush))
      ::DeleteObject(obrush);

    mStates->mSolidBrush = tbrush;
    mStates->mBrushColor = mCurrentColor;
  }

  if (NULL == mStates->mSolidBrush)
  {
    GraphicsState *tstate = mStates->mNext;

    while ((nsnull != tstate) && (NULL == tstate->mSolidBrush))
      tstate = tstate->mNext;

    if (nsnull == tstate)
      return mBlackBrush;
    else
      return tstate->mSolidBrush;
  }
  else
    return mStates->mSolidBrush;
}

void nsRenderingContextWin :: SetupFont(void)
{
  if (mFontMetrics != mStates->mFontMetrics)
  {
    HFONT   tfont = (HFONT)mFontMetrics->GetFontHandle();
    
    ::SelectObject(mDC, tfont);
    mStates->mFont = tfont;
    mStates->mFontMetrics = mFontMetrics;
  }
}

HPEN nsRenderingContextWin :: SetupSolidPen(void)
{
  if (mCurrentColor != mStates->mPenColor)
  {
    HPEN  tpen = ::CreatePen(PS_SOLID, 0, mColor);
    HPEN  open = ::SelectObject(mDC, tpen);

    if ((NULL != open) && (NULL != mStates->mSolidPen))
      ::DeleteObject(open);

    mStates->mSolidPen = tpen;
    mStates->mPenColor = mCurrentColor;
  }

  if (NULL == mStates->mSolidPen)
  {
    GraphicsState *tstate = mStates->mNext;

    while ((nsnull != tstate) && (NULL == tstate->mSolidPen))
      tstate = tstate->mNext;

    if (nsnull == tstate)
      return mBlackPen;
    else
      return tstate->mSolidPen;
  }
  else
    return mStates->mSolidPen;
}

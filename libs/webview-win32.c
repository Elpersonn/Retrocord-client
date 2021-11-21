/*
 * MIT License
 *
 * Copyright (c) 2017 Serge Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

#define CINTERFACE
#include <windows.h>

#include <commctrl.h>
#include <exdisp.h>
#include <mshtmhst.h>
#include <mshtml.h>
#include <shobjidl.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct webview2_struct webview2;

struct webview_priv {
  HWND hwnd;
  IOleObject **browser;
  BOOL is_fullscreen;
  DWORD saved_style;
  DWORD saved_ex_style;
  RECT saved_rect;
  webview2 *webview2;
};

#define WM_WEBVIEW_DISPATCH (WM_APP + 1)
#define WM_WEBVIEW_READY (WM_APP + 2)

typedef struct {
  IOleInPlaceFrame frame;
  HWND window;
} _IOleInPlaceFrameEx;

typedef struct {
  IOleInPlaceSite inplace;
  _IOleInPlaceFrameEx frame;
} _IOleInPlaceSiteEx;

typedef struct {
  IDocHostUIHandler ui;
} _IDocHostUIHandlerEx;

typedef struct {
  IInternetSecurityManager mgr;
} _IInternetSecurityManagerEx;

typedef struct {
  IServiceProvider provider;
  _IInternetSecurityManagerEx mgr;
} _IServiceProviderEx;

typedef struct {
  IOleClientSite client;
  _IOleInPlaceSiteEx inplace;
  _IDocHostUIHandlerEx ui;
  IDispatch external;
  _IServiceProviderEx provider;
} _IOleClientSiteEx;

#ifdef __cplusplus
#define iid_ref(x) &(x)
#define iid_unref(x) *(x)
#else
#define iid_ref(x) (x)
#define iid_unref(x) (x)
#endif

static inline WCHAR *webview_to_utf16(const char *s) {
  DWORD size = MultiByteToWideChar(CP_UTF8, 0, s, -1, 0, 0);
  WCHAR *ws = (WCHAR *)GlobalAlloc(GMEM_FIXED, sizeof(WCHAR) * size);
  if (ws == NULL) {
    return NULL;
  }
  MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, size);
  return ws;
}

static inline char *webview_from_utf16(WCHAR *ws) {
  int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
  char *s = (char *)GlobalAlloc(GMEM_FIXED, n);
  if (s == NULL) {
    return NULL;
  }
  WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, n, NULL, NULL);
  return s;
}

static int iid_eq(REFIID a, const IID *b) {
  return memcmp((const void *)iid_ref(a), (const void *)b, sizeof(GUID)) == 0;
}

static HRESULT STDMETHODCALLTYPE WV_JS_QueryInterface(IDispatch FAR *This,
                                                   REFIID riid,
                                                   LPVOID FAR *ppvObj) {
  if (iid_eq(riid, &IID_IDispatch)) {
    *ppvObj = This;
    return S_OK;
  }
  *ppvObj = 0;
  return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE WV_JS_AddRef(IDispatch FAR *This) { return 1; }
static ULONG STDMETHODCALLTYPE WV_JS_Release(IDispatch FAR *This) { return 1; }
static HRESULT STDMETHODCALLTYPE WV_JS_GetTypeInfoCount(IDispatch FAR *This,
                                                     UINT *pctinfo) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE WV_JS_GetTypeInfo(IDispatch FAR *This,
                                                UINT iTInfo, LCID lcid,
                                                ITypeInfo **ppTInfo) {
  return S_OK;
}
#define WEBVIEW_JS_INVOKE_ID 0x1000
static HRESULT STDMETHODCALLTYPE WV_JS_GetIDsOfNames(IDispatch FAR *This,
                                                  REFIID riid,
                                                  LPOLESTR *rgszNames,
                                                  UINT cNames, LCID lcid,
                                                  DISPID *rgDispId) {
  if (cNames != 1) {
    return S_FALSE;
  }
  if (wcscmp(rgszNames[0], L"invoke") == 0) {
    rgDispId[0] = WEBVIEW_JS_INVOKE_ID;
    return S_OK;
  }
  return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE
WV_JS_Invoke(IDispatch FAR *This, DISPID dispIdMember, REFIID riid, LCID lcid,
          WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult,
          EXCEPINFO *pExcepInfo, UINT *puArgErr) {
  size_t offset = (size_t) & ((_IOleClientSiteEx *)NULL)->external;
  _IOleClientSiteEx *ex = (_IOleClientSiteEx *)((char *)(This)-offset);
  struct webview *w = (struct webview *)GetWindowLongPtr(
      ex->inplace.frame.window, GWLP_USERDATA);
  if (pDispParams->cArgs == 1 && pDispParams->rgvarg[0].vt == VT_BSTR) {
    BSTR bstr = pDispParams->rgvarg[0].bstrVal;
    char *s = webview_from_utf16(bstr);
    if (s != NULL) {
      if (dispIdMember == WEBVIEW_JS_INVOKE_ID) {
        if (w->external_invoke_cb != NULL) {
          w->external_invoke_cb(w, s);
        }
      } else {
        return S_FALSE;
      }
      GlobalFree(s);
    }
  }
  return S_OK;
}

static IDispatchVtbl ExternalDispatchTable = {
    WV_JS_QueryInterface, WV_JS_AddRef,        WV_JS_Release, WV_JS_GetTypeInfoCount,
    WV_JS_GetTypeInfo,    WV_JS_GetIDsOfNames, WV_JS_Invoke};

static ULONG STDMETHODCALLTYPE Site_AddRef(IOleClientSite FAR *This) {
  return 1;
}
static ULONG STDMETHODCALLTYPE Site_Release(IOleClientSite FAR *This) {
  return 1;
}
static HRESULT STDMETHODCALLTYPE Site_SaveObject(IOleClientSite FAR *This) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Site_GetMoniker(IOleClientSite FAR *This,
                                                 DWORD dwAssign,
                                                 DWORD dwWhichMoniker,
                                                 IMoniker **ppmk) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE
Site_GetContainer(IOleClientSite FAR *This, LPOLECONTAINER FAR *ppContainer) {
  *ppContainer = 0;
  return E_NOINTERFACE;
}
static HRESULT STDMETHODCALLTYPE Site_ShowObject(IOleClientSite FAR *This) {
  return NOERROR;
}
static HRESULT STDMETHODCALLTYPE Site_OnShowWindow(IOleClientSite FAR *This,
                                                   BOOL fShow) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE
Site_RequestNewObjectLayout(IOleClientSite FAR *This) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Site_QueryInterface(IOleClientSite FAR *This,
                                                     REFIID riid,
                                                     void **ppvObject) {
  if (iid_eq(riid, &IID_IUnknown) || iid_eq(riid, &IID_IOleClientSite)) {
    *ppvObject = &((_IOleClientSiteEx *)This)->client;
  } else if (iid_eq(riid, &IID_IOleInPlaceSite)) {
    *ppvObject = &((_IOleClientSiteEx *)This)->inplace;
  } else if (iid_eq(riid, &IID_IDocHostUIHandler)) {
    *ppvObject = &((_IOleClientSiteEx *)This)->ui;
  } else if (iid_eq(riid, &IID_IServiceProvider)) {
    *ppvObject = &((_IOleClientSiteEx *)This)->provider;
  } else {
    *ppvObject = 0;
    return (E_NOINTERFACE);
  }
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE InPlace_QueryInterface(
    IOleInPlaceSite FAR *This, REFIID riid, LPVOID FAR *ppvObj) {
  return (Site_QueryInterface(
      (IOleClientSite *)((char *)This - sizeof(IOleClientSite)), riid, ppvObj));
}
static ULONG STDMETHODCALLTYPE InPlace_AddRef(IOleInPlaceSite FAR *This) {
  return 1;
}
static ULONG STDMETHODCALLTYPE InPlace_Release(IOleInPlaceSite FAR *This) {
  return 1;
}
static HRESULT STDMETHODCALLTYPE InPlace_GetWindow(IOleInPlaceSite FAR *This,
                                                   HWND FAR *lphwnd) {
  *lphwnd = ((_IOleInPlaceSiteEx FAR *)This)->frame.window;
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
InPlace_ContextSensitiveHelp(IOleInPlaceSite FAR *This, BOOL fEnterMode) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE
InPlace_CanInPlaceActivate(IOleInPlaceSite FAR *This) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
InPlace_OnInPlaceActivate(IOleInPlaceSite FAR *This) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
InPlace_OnUIActivate(IOleInPlaceSite FAR *This) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE InPlace_GetWindowContext(
    IOleInPlaceSite FAR *This, LPOLEINPLACEFRAME FAR *lplpFrame,
    LPOLEINPLACEUIWINDOW FAR *lplpDoc, LPRECT lprcPosRect, LPRECT lprcClipRect,
    LPOLEINPLACEFRAMEINFO lpFrameInfo) {
  *lplpFrame = (LPOLEINPLACEFRAME) & ((_IOleInPlaceSiteEx *)This)->frame;
  *lplpDoc = 0;
  lpFrameInfo->fMDIApp = FALSE;
  lpFrameInfo->hwndFrame = ((_IOleInPlaceFrameEx *)*lplpFrame)->window;
  lpFrameInfo->haccel = 0;
  lpFrameInfo->cAccelEntries = 0;
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE InPlace_Scroll(IOleInPlaceSite FAR *This,
                                                SIZE scrollExtent) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE
InPlace_OnUIDeactivate(IOleInPlaceSite FAR *This, BOOL fUndoable) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
InPlace_OnInPlaceDeactivate(IOleInPlaceSite FAR *This) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
InPlace_DiscardUndoState(IOleInPlaceSite FAR *This) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE
InPlace_DeactivateAndUndo(IOleInPlaceSite FAR *This) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE
InPlace_OnPosRectChange(IOleInPlaceSite FAR *This, LPCRECT lprcPosRect) {
  IOleObject *browserObject;
  IOleInPlaceObject *inplace;
  browserObject = *((IOleObject **)((char *)This - sizeof(IOleObject *) -
                                    sizeof(IOleClientSite)));
  if (!browserObject->lpVtbl->QueryInterface(browserObject,
                                             iid_unref(&IID_IOleInPlaceObject),
                                             (void **)&inplace)) {
    inplace->lpVtbl->SetObjectRects(inplace, lprcPosRect, lprcPosRect);
    inplace->lpVtbl->Release(inplace);
  }
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE Frame_QueryInterface(
    IOleInPlaceFrame FAR *This, REFIID riid, LPVOID FAR *ppvObj) {
  return E_NOTIMPL;
}
static ULONG STDMETHODCALLTYPE Frame_AddRef(IOleInPlaceFrame FAR *This) {
  return 1;
}
static ULONG STDMETHODCALLTYPE Frame_Release(IOleInPlaceFrame FAR *This) {
  return 1;
}
static HRESULT STDMETHODCALLTYPE Frame_GetWindow(IOleInPlaceFrame FAR *This,
                                                 HWND FAR *lphwnd) {
  *lphwnd = ((_IOleInPlaceFrameEx *)This)->window;
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
Frame_ContextSensitiveHelp(IOleInPlaceFrame FAR *This, BOOL fEnterMode) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Frame_GetBorder(IOleInPlaceFrame FAR *This,
                                                 LPRECT lprectBorder) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Frame_RequestBorderSpace(
    IOleInPlaceFrame FAR *This, LPCBORDERWIDTHS pborderwidths) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Frame_SetBorderSpace(
    IOleInPlaceFrame FAR *This, LPCBORDERWIDTHS pborderwidths) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Frame_SetActiveObject(
    IOleInPlaceFrame FAR *This, IOleInPlaceActiveObject *pActiveObject,
    LPCOLESTR pszObjName) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
Frame_InsertMenus(IOleInPlaceFrame FAR *This, HMENU hmenuShared,
                  LPOLEMENUGROUPWIDTHS lpMenuWidths) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Frame_SetMenu(IOleInPlaceFrame FAR *This,
                                               HMENU hmenuShared,
                                               HOLEMENU holemenu,
                                               HWND hwndActiveObject) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE Frame_RemoveMenus(IOleInPlaceFrame FAR *This,
                                                   HMENU hmenuShared) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE Frame_SetStatusText(IOleInPlaceFrame FAR *This,
                                                     LPCOLESTR pszStatusText) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
Frame_EnableModeless(IOleInPlaceFrame FAR *This, BOOL fEnable) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
Frame_TranslateAccelerator(IOleInPlaceFrame FAR *This, LPMSG lpmsg, WORD wID) {
  return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE UI_QueryInterface(IDocHostUIHandler FAR *This,
                                                   REFIID riid,
                                                   LPVOID FAR *ppvObj) {
  return (Site_QueryInterface((IOleClientSite *)((char *)This -
                                                 sizeof(IOleClientSite) -
                                                 sizeof(_IOleInPlaceSiteEx)),
                              riid, ppvObj));
}
static ULONG STDMETHODCALLTYPE UI_AddRef(IDocHostUIHandler FAR *This) {
  return 1;
}
static ULONG STDMETHODCALLTYPE UI_Release(IDocHostUIHandler FAR *This) {
  return 1;
}
static HRESULT STDMETHODCALLTYPE UI_ShowContextMenu(
    IDocHostUIHandler FAR *This, DWORD dwID, POINT __RPC_FAR *ppt,
    IUnknown __RPC_FAR *pcmdtReserved, IDispatch __RPC_FAR *pdispReserved) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
UI_GetHostInfo(IDocHostUIHandler FAR *This, DOCHOSTUIINFO __RPC_FAR *pInfo) {
  pInfo->cbSize = sizeof(DOCHOSTUIINFO);
  pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER;
  pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE UI_ShowUI(
    IDocHostUIHandler FAR *This, DWORD dwID,
    IOleInPlaceActiveObject __RPC_FAR *pActiveObject,
    IOleCommandTarget __RPC_FAR *pCommandTarget,
    IOleInPlaceFrame __RPC_FAR *pFrame, IOleInPlaceUIWindow __RPC_FAR *pDoc) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE UI_HideUI(IDocHostUIHandler FAR *This) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE UI_UpdateUI(IDocHostUIHandler FAR *This) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE UI_EnableModeless(IDocHostUIHandler FAR *This,
                                                   BOOL fEnable) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
UI_OnDocWindowActivate(IDocHostUIHandler FAR *This, BOOL fActivate) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
UI_OnFrameWindowActivate(IDocHostUIHandler FAR *This, BOOL fActivate) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
UI_ResizeBorder(IDocHostUIHandler FAR *This, LPCRECT prcBorder,
                IOleInPlaceUIWindow __RPC_FAR *pUIWindow, BOOL fRameWindow) {
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE
UI_TranslateAccelerator(IDocHostUIHandler FAR *This, LPMSG lpMsg,
                        const GUID __RPC_FAR *pguidCmdGroup, DWORD nCmdID) {
  return S_FALSE;
}
static HRESULT STDMETHODCALLTYPE UI_GetOptionKeyPath(
    IDocHostUIHandler FAR *This, LPOLESTR __RPC_FAR *pchKey, DWORD dw) {
  return S_FALSE;
}
static HRESULT STDMETHODCALLTYPE UI_GetDropTarget(
    IDocHostUIHandler FAR *This, IDropTarget __RPC_FAR *pDropTarget,
    IDropTarget __RPC_FAR *__RPC_FAR *ppDropTarget) {
  return S_FALSE;
}
static HRESULT STDMETHODCALLTYPE UI_GetExternal(
    IDocHostUIHandler FAR *This, IDispatch __RPC_FAR *__RPC_FAR *ppDispatch) {
  *ppDispatch = (IDispatch *)(This + 1);
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE UI_TranslateUrl(
    IDocHostUIHandler FAR *This, DWORD dwTranslate, OLECHAR __RPC_FAR *pchURLIn,
    OLECHAR __RPC_FAR *__RPC_FAR *ppchURLOut) {
  *ppchURLOut = 0;
  return S_FALSE;
}
static HRESULT STDMETHODCALLTYPE
UI_FilterDataObject(IDocHostUIHandler FAR *This, IDataObject __RPC_FAR *pDO,
                    IDataObject __RPC_FAR *__RPC_FAR *ppDORet) {
  *ppDORet = 0;
  return S_FALSE;
}

static const TCHAR *classname = "WebView";
static const SAFEARRAYBOUND ArrayBound = {1, 0};

static IOleClientSiteVtbl MyIOleClientSiteTable = {
    Site_QueryInterface, Site_AddRef,       Site_Release,
    Site_SaveObject,     Site_GetMoniker,   Site_GetContainer,
    Site_ShowObject,     Site_OnShowWindow, Site_RequestNewObjectLayout};
static IOleInPlaceSiteVtbl MyIOleInPlaceSiteTable = {
    InPlace_QueryInterface,
    InPlace_AddRef,
    InPlace_Release,
    InPlace_GetWindow,
    InPlace_ContextSensitiveHelp,
    InPlace_CanInPlaceActivate,
    InPlace_OnInPlaceActivate,
    InPlace_OnUIActivate,
    InPlace_GetWindowContext,
    InPlace_Scroll,
    InPlace_OnUIDeactivate,
    InPlace_OnInPlaceDeactivate,
    InPlace_DiscardUndoState,
    InPlace_DeactivateAndUndo,
    InPlace_OnPosRectChange};

static IOleInPlaceFrameVtbl MyIOleInPlaceFrameTable = {
    Frame_QueryInterface,
    Frame_AddRef,
    Frame_Release,
    Frame_GetWindow,
    Frame_ContextSensitiveHelp,
    Frame_GetBorder,
    Frame_RequestBorderSpace,
    Frame_SetBorderSpace,
    Frame_SetActiveObject,
    Frame_InsertMenus,
    Frame_SetMenu,
    Frame_RemoveMenus,
    Frame_SetStatusText,
    Frame_EnableModeless,
    Frame_TranslateAccelerator};

static IDocHostUIHandlerVtbl MyIDocHostUIHandlerTable = {
    UI_QueryInterface,
    UI_AddRef,
    UI_Release,
    UI_ShowContextMenu,
    UI_GetHostInfo,
    UI_ShowUI,
    UI_HideUI,
    UI_UpdateUI,
    UI_EnableModeless,
    UI_OnDocWindowActivate,
    UI_OnFrameWindowActivate,
    UI_ResizeBorder,
    UI_TranslateAccelerator,
    UI_GetOptionKeyPath,
    UI_GetDropTarget,
    UI_GetExternal,
    UI_TranslateUrl,
    UI_FilterDataObject};



static HRESULT STDMETHODCALLTYPE IS_QueryInterface(IInternetSecurityManager FAR *This, REFIID riid, void **ppvObject) {
  return E_NOTIMPL;
}
static ULONG STDMETHODCALLTYPE IS_AddRef(IInternetSecurityManager FAR *This) { return 1; }
static ULONG STDMETHODCALLTYPE IS_Release(IInternetSecurityManager FAR *This) { return 1; }
static HRESULT STDMETHODCALLTYPE IS_SetSecuritySite(IInternetSecurityManager FAR *This, IInternetSecurityMgrSite *pSited) {
  return INET_E_DEFAULT_ACTION;
}
static HRESULT STDMETHODCALLTYPE IS_GetSecuritySite(IInternetSecurityManager FAR *This, IInternetSecurityMgrSite **ppSite) {
  return INET_E_DEFAULT_ACTION;
}
static HRESULT STDMETHODCALLTYPE IS_MapUrlToZone(IInternetSecurityManager FAR *This, LPCWSTR pwszUrl, DWORD *pdwZone, DWORD dwFlags) {
  *pdwZone = URLZONE_LOCAL_MACHINE;
  return S_OK;
}
static HRESULT STDMETHODCALLTYPE IS_GetSecurityId(IInternetSecurityManager FAR *This, LPCWSTR pwszUrl, BYTE *pbSecurityId, DWORD *pcbSecurityId, DWORD_PTR dwReserved) {
  return INET_E_DEFAULT_ACTION;
}
static HRESULT STDMETHODCALLTYPE IS_ProcessUrlAction(IInternetSecurityManager FAR *This, LPCWSTR pwszUrl, DWORD dwAction, BYTE *pPolicy,  DWORD cbPolicy, BYTE *pContext, DWORD cbContext, DWORD dwFlags, DWORD dwReserved) {
  return INET_E_DEFAULT_ACTION;
}
static HRESULT STDMETHODCALLTYPE IS_QueryCustomPolicy(IInternetSecurityManager FAR *This, LPCWSTR pwszUrl, REFGUID guidKey, BYTE **ppPolicy, DWORD *pcbPolicy, BYTE *pContext, DWORD cbContext, DWORD dwReserved) {
  return INET_E_DEFAULT_ACTION;
}
static HRESULT STDMETHODCALLTYPE IS_SetZoneMapping(IInternetSecurityManager FAR *This, DWORD dwZone, LPCWSTR lpszPattern, DWORD dwFlags) {
  return INET_E_DEFAULT_ACTION;
}
static HRESULT STDMETHODCALLTYPE IS_GetZoneMappings(IInternetSecurityManager FAR *This, DWORD dwZone, IEnumString **ppenumString, DWORD dwFlags) {
  return INET_E_DEFAULT_ACTION;
}
static IInternetSecurityManagerVtbl MyInternetSecurityManagerTable = {IS_QueryInterface, IS_AddRef, IS_Release, IS_SetSecuritySite, IS_GetSecuritySite, IS_MapUrlToZone, IS_GetSecurityId, IS_ProcessUrlAction, IS_QueryCustomPolicy, IS_SetZoneMapping, IS_GetZoneMappings};

static HRESULT STDMETHODCALLTYPE SP_QueryInterface(IServiceProvider FAR *This, REFIID riid, void **ppvObject) {
  return (Site_QueryInterface(
      (IOleClientSite *)((char *)This - sizeof(IOleClientSite) - sizeof(_IOleInPlaceSiteEx) - sizeof(_IDocHostUIHandlerEx) - sizeof(IDispatch)), riid, ppvObject));
}
static ULONG STDMETHODCALLTYPE SP_AddRef(IServiceProvider FAR *This) { return 1; }
static ULONG STDMETHODCALLTYPE SP_Release(IServiceProvider FAR *This) { return 1; }
static HRESULT STDMETHODCALLTYPE SP_QueryService(IServiceProvider FAR *This, REFGUID siid, REFIID riid, void **ppvObject) {
  if (iid_eq(siid, &IID_IInternetSecurityManager) && iid_eq(riid, &IID_IInternetSecurityManager)) {
    *ppvObject = &((_IServiceProviderEx *)This)->mgr;
  } else {
    *ppvObject = 0;
    return (E_NOINTERFACE);
  }
  return S_OK;
}
static IServiceProviderVtbl MyServiceProviderTable = {SP_QueryInterface, SP_AddRef, SP_Release, SP_QueryService};

static void UnEmbedBrowserObject(struct webview *w) {
  if (w->priv->browser != NULL) {
    (*w->priv->browser)->lpVtbl->Close(*w->priv->browser, OLECLOSE_NOSAVE);
    (*w->priv->browser)->lpVtbl->Release(*w->priv->browser);
    GlobalFree(w->priv->browser);
    w->priv->browser = NULL;
  }
}

static int EmbedBrowserObject(struct webview *w) {
  RECT rect;
  IWebBrowser2 *webBrowser2 = NULL;
  LPCLASSFACTORY pClassFactory = NULL;
  _IOleClientSiteEx *_iOleClientSiteEx = NULL;
  IOleObject **browser = (IOleObject **)GlobalAlloc(
      GMEM_FIXED, sizeof(IOleObject *) + sizeof(_IOleClientSiteEx));
  if (browser == NULL) {
    goto error;
  }
  w->priv->browser = browser;

  _iOleClientSiteEx = (_IOleClientSiteEx *)(browser + 1);
  _iOleClientSiteEx->client.lpVtbl = &MyIOleClientSiteTable;
  _iOleClientSiteEx->inplace.inplace.lpVtbl = &MyIOleInPlaceSiteTable;
  _iOleClientSiteEx->inplace.frame.frame.lpVtbl = &MyIOleInPlaceFrameTable;
  _iOleClientSiteEx->inplace.frame.window = w->priv->hwnd;
  _iOleClientSiteEx->ui.ui.lpVtbl = &MyIDocHostUIHandlerTable;
  _iOleClientSiteEx->external.lpVtbl = &ExternalDispatchTable;
  _iOleClientSiteEx->provider.provider.lpVtbl = &MyServiceProviderTable;
  _iOleClientSiteEx->provider.mgr.mgr.lpVtbl = &MyInternetSecurityManagerTable;

  if (CoGetClassObject(iid_unref(&CLSID_WebBrowser),
                       CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER, NULL,
                       iid_unref(&IID_IClassFactory),
                       (void **)&pClassFactory) != S_OK) {
    goto error;
  }

  if (pClassFactory == NULL) {
    goto error;
  }

  if (pClassFactory->lpVtbl->CreateInstance(pClassFactory, 0,
                                            iid_unref(&IID_IOleObject),
                                            (void **)browser) != S_OK) {
    goto error;
  }
  pClassFactory->lpVtbl->Release(pClassFactory);
  if ((*browser)->lpVtbl->SetClientSite(
          *browser, (IOleClientSite *)_iOleClientSiteEx) != S_OK) {
    goto error;
  }
  (*browser)->lpVtbl->SetHostNames(*browser, L"My Host Name", 0);

  if (OleSetContainedObject((struct IUnknown *)(*browser), TRUE) != S_OK) {
    goto error;
  }
  GetClientRect(w->priv->hwnd, &rect);
  if ((*browser)->lpVtbl->DoVerb((*browser), OLEIVERB_SHOW, NULL,
                                 (IOleClientSite *)_iOleClientSiteEx, -1,
                                 w->priv->hwnd, &rect) != S_OK) {
    goto error;
  }
  if ((*browser)->lpVtbl->QueryInterface((*browser),
                                         iid_unref(&IID_IWebBrowser2),
                                         (void **)&webBrowser2) != S_OK) {
    goto error;
  }

  webBrowser2->lpVtbl->put_Left(webBrowser2, 0);
  webBrowser2->lpVtbl->put_Top(webBrowser2, 0);
  webBrowser2->lpVtbl->put_Width(webBrowser2, rect.right);
  webBrowser2->lpVtbl->put_Height(webBrowser2, rect.bottom);
  webBrowser2->lpVtbl->Release(webBrowser2);

  return 0;
error:
  UnEmbedBrowserObject(w);
  if (pClassFactory != NULL) {
    pClassFactory->lpVtbl->Release(pClassFactory);
  }
  if (browser != NULL) {
    GlobalFree(browser);
  }
  return -1;
}

#define WEBVIEW_DATA_URL_PREFIX "data:text/html,"
static int DisplayHTMLPage(struct webview *w) {
  IWebBrowser2 *webBrowser2;
  VARIANT myURL;
  LPDISPATCH lpDispatch;
  IHTMLDocument2 *htmlDoc2;
  BSTR bstr;
  IOleObject *browserObject;
  SAFEARRAY *sfArray;
  VARIANT *pVar;
  browserObject = *w->priv->browser;
  int isDataURL = 0;
  const char *webview_url = webview_check_url(w->url);
  if (!browserObject->lpVtbl->QueryInterface(
          browserObject, iid_unref(&IID_IWebBrowser2), (void **)&webBrowser2)) {
    LPCSTR webPageName;
    isDataURL = (strncmp(webview_url, WEBVIEW_DATA_URL_PREFIX,
                         strlen(WEBVIEW_DATA_URL_PREFIX)) == 0);
#ifdef WEBVIEW_SUPRESS_ERRORS
	webBrowser2->lpVtbl->put_Silent(webBrowser2, true);
#endif
    if (isDataURL) {
      webPageName = "about:blank";
    } else {
      webPageName = (LPCSTR)webview_url;
    }
    VariantInit(&myURL);
    myURL.vt = VT_BSTR;
#ifndef UNICODE
    {
      wchar_t *buffer = webview_to_utf16(webPageName);
      if (buffer == NULL) {
        goto badalloc;
      }
      myURL.bstrVal = SysAllocString(buffer);
      GlobalFree(buffer);
    }
#else
    myURL.bstrVal = SysAllocString(webPageName);
#endif
    if (!myURL.bstrVal) {
    badalloc:
      webBrowser2->lpVtbl->Release(webBrowser2);
      return (-6);
    }
    webBrowser2->lpVtbl->Navigate2(webBrowser2, &myURL, 0, 0, 0, 0);
    VariantClear(&myURL);
    if (!isDataURL) {
      return 0;
    }

    char *url = (char *)calloc(1, strlen(webview_url) + 1);
    char *q = url;
    for (const char *p = webview_url + strlen(WEBVIEW_DATA_URL_PREFIX); (*q = *p);
         p++, q++) {
      if (*q == '%' && *(p + 1) && *(p + 2)) {
        sscanf(p + 1, "%02x", (unsigned int*)q);
        p = p + 2;
      }
    }

    if (webBrowser2->lpVtbl->get_Document(webBrowser2, &lpDispatch) == S_OK) {
      if (lpDispatch->lpVtbl->QueryInterface(lpDispatch,
                                             iid_unref(&IID_IHTMLDocument2),
                                             (void **)&htmlDoc2) == S_OK) {
        if ((sfArray = SafeArrayCreate(VT_VARIANT, 1,
                                       (SAFEARRAYBOUND *)&ArrayBound))) {
          if (!SafeArrayAccessData(sfArray, (void **)&pVar)) {
            pVar->vt = VT_BSTR;
#ifndef UNICODE
            {
              wchar_t *buffer = webview_to_utf16(url);
              if (buffer == NULL) {
                goto release;
              }
              bstr = SysAllocString(buffer);
              GlobalFree(buffer);
            }
#else
            bstr = SysAllocString(string);
#endif
            if ((pVar->bstrVal = bstr)) {
              htmlDoc2->lpVtbl->write(htmlDoc2, sfArray);
              htmlDoc2->lpVtbl->close(htmlDoc2);
            }
          }
          SafeArrayDestroy(sfArray);
        }
      release:
        free(url);
        htmlDoc2->lpVtbl->Release(htmlDoc2);
      }
      lpDispatch->lpVtbl->Release(lpDispatch);
    }
    webBrowser2->lpVtbl->Release(webBrowser2);
    return (0);
  }
  return (-5);
}

#include "webview-ms-webview2.h"

#define WEBVIEW2_WIN32_DISABLE_AUTO_DETECT "WEBVIEW2_WIN32_DISABLE_AUTO_DETECT"
#define WEBVIEW2_WIN32_PATH "WEBVIEW2_WIN32_PATH"
#define WEBVIEW2_WIN32_USER_DATA_NO_APPDATA "WEBVIEW2_WIN32_USER_DATA_NO_APPDATA"

#define WEBVIEW2_BROWSER_EXECUTABLE_FOLDER "WEBVIEW2_BROWSER_EXECUTABLE_FOLDER"
#define WEBVIEW2_USER_DATA_FOLDER "WEBVIEW2_USER_DATA_FOLDER"

#define WEBVIEW2_RT_UUID "{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"
#define EDGE_STABLE_UUID "{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}"

#define KEY_CLIENTS_LOCATION "location"
#define KEY_CLIENTS_PV "pv"
#define KEY_MS_EDGEUPDATE_CLIENTSTATE "SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\ClientState"
#define KEY_MS_EDGEUPDATE_CLIENTS "SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients"

#define WEBVIEW2_LOADER_DLL "WebView2Loader.dll"

static int fileExists(LPCSTR lpFileName) {
  return GetFileAttributes(lpFileName) != INVALID_FILE_ATTRIBUTES;
}
static int isDirectory(LPCSTR lpFileName) {
  DWORD attributes = GetFileAttributes(lpFileName);
  return (attributes != INVALID_FILE_ATTRIBUTES) && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static void findWebView2BrowserExecutableFolder() {
  char data[MAX_PATH];
  char version[64];
  char path[MAX_PATH];
  char *bs;
  DWORD size;
  if ((getenv(WEBVIEW2_BROWSER_EXECUTABLE_FOLDER) != NULL) || (getenv(WEBVIEW2_WIN32_DISABLE_AUTO_DETECT) != NULL)) {
    return;
  }
  /*
  see https://docs.microsoft.com/en-gb/deployedge/microsoft-edge-webview-policies
  see https://docs.microsoft.com/en-gb/microsoft-edge/webview2/concepts/distribution
  see https://docs.microsoft.com/en-gb/microsoft-edge/webview2/reference/win32/webview2-idl?view=webview2-0.9.622#createcorewebview2environmentwithoptions
  To use a fixed version of the WebView2 Runtime, pass the relative path of the folder
  that contains the fixed version of the WebView2 Runtime to browserExecutableFolder.
  The path of fixed version of the WebView2 Runtime should not contain \Edge\Application\.
  When such a path is used, the API will fail with ERROR_NOT_SUPPORTED.
  */
  size = sizeof(data);
  if (RegGetValueA(HKEY_LOCAL_MACHINE, KEY_MS_EDGEUPDATE_CLIENTS "\\" EDGE_STABLE_UUID, KEY_CLIENTS_LOCATION, RRF_RT_REG_SZ, NULL, &data, &size) == ERROR_SUCCESS) {
    size = sizeof(version);
    if (isDirectory(data) && (RegGetValueA(HKEY_LOCAL_MACHINE, KEY_MS_EDGEUPDATE_CLIENTS "\\" EDGE_STABLE_UUID, KEY_CLIENTS_PV, RRF_RT_REG_SZ, NULL, &version, &size) == ERROR_SUCCESS)) {
      bs = strrchr(data, '\\');
      if (bs != NULL) {
        *bs = '-';
        sprintf(path, "%s\\%s\\msedge.dll", data, version);
        if (fileExists(path)) {
          sprintf(path, "%s\\%s", data, version);
          webview_print_log("found Edge alternative folder, use " WEBVIEW2_WIN32_DISABLE_AUTO_DETECT " to disable");
          webview_print_log(path);
          SetEnvironmentVariable(WEBVIEW2_BROWSER_EXECUTABLE_FOLDER, path);
        }
      }
    }
  }
}

static TCHAR *getWebView2LoaderFileName(TCHAR *modulePath) {
  char * webView2Win32Path = getenv(WEBVIEW2_WIN32_PATH);
  if ((webView2Win32Path != NULL) && (strlen(webView2Win32Path) > MAX_PATH)) {
    webView2Win32Path = NULL;
  }
  if (webView2Win32Path == NULL) {
    strcpy(modulePath, WEBVIEW2_LOADER_DLL);
  } else {
    sprintf(modulePath, "%s\\" WEBVIEW2_LOADER_DLL, webView2Win32Path);
  }
  return modulePath;
}

static WCHAR *getUserData(WCHAR *buffer, size_t sizeOfBuffer) {
  WCHAR filename[MAX_PATH];
  WCHAR *appData = _wgetenv(L"APPDATA");
  char *noAppData = getenv(WEBVIEW2_WIN32_USER_DATA_NO_APPDATA);
  if ((getenv(WEBVIEW2_USER_DATA_FOLDER) == NULL) && (appData != NULL) && (noAppData == NULL)) {
    GetModuleFileNameW(NULL, filename, MAX_PATH);
    WCHAR *executableName = wcsrchr(filename, L'\\');
    if (executableName != NULL) {
      executableName++;
    } else {
      executableName = filename;
    }
    swprintf(buffer, sizeOfBuffer, L"%s\\%s.WebView2", appData, executableName);
    webview_print_log("getUserData()");
    OutputDebugStringW(buffer);
    return buffer;
  }
  return NULL;
}


#define WEBVIEW2_WIN32_API extern

typedef void (*WebView2CallbackFn) (webview2 *wv, const char *message, void *context);

#define GET_AVAILABLE_COREWEBVIEW2_BROWSER_VERSION_FN_NAME "GetAvailableCoreWebView2BrowserVersionString"
#define CREATE_COREWEBVIEW2_ENVIRONMENTWITHOPTIONS_FN_NAME "CreateCoreWebView2EnvironmentWithOptions"

typedef HRESULT (*GetWebView2BrowserVersionInfoFnType) (PCWSTR browserExecutableFolder, LPWSTR* versionInfo);
typedef HRESULT (*CreateCoreWebView2EnvironmentWithOptionsFnType) (PCWSTR browserExecutableFolder, PCWSTR userDataFolder, ICoreWebView2EnvironmentOptions* environmentOptions, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environment_created_handler);

typedef struct webview2_struct {
  HWND hwnd;
  const char *url;
  int debug;
  WebView2CallbackFn cb;
  void *context;
  ICoreWebView2 *webview;
  unsigned char ready;
  char *jsToEval;
  ICoreWebView2Controller *controller;
  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler env_created_handler;
  ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl env_created_handler_vtbl;
  ICoreWebView2CreateCoreWebView2ControllerCompletedHandler controller_created_handler;
  ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl controller_created_handler_vtbl;
  ICoreWebView2WebMessageReceivedEventHandler message_received_event_handler;
  ICoreWebView2WebMessageReceivedEventHandlerVtbl message_received_event_handler_vtbl;
  ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler add_script_on_document_created_handler;
  ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandlerVtbl add_script_on_document_created_handler_vtbl;
} webview2;

#define WEBVIEW2_PTR_FROM(_cp, _field) \
  ((webview2 *) ((char *) (_cp) - offsetof(webview2, _field)))

static CreateCoreWebView2EnvironmentWithOptionsFnType CreateCoreWebView2EnvironmentFn = NULL;

static HRESULT WebView2WebMessageReceivedEventHandleInvoke(ICoreWebView2WebMessageReceivedEventHandler * This, ICoreWebView2 *webView, ICoreWebView2WebMessageReceivedEventArgs *args) {
  LPWSTR webMessage;
  HRESULT hRes;
  webview2 *pwv2 = WEBVIEW2_PTR_FROM(This, message_received_event_handler);
  hRes = args->lpVtbl->TryGetWebMessageAsString(args, &webMessage);
  if (hRes == E_INVALIDARG) {
    hRes = args->lpVtbl->get_WebMessageAsJson(args, &webMessage);
  }
  if (pwv2->cb != NULL) {
    char *message = webview_from_utf16(webMessage);
    if (message != NULL) {
      pwv2->cb(pwv2, message, pwv2->context);
      GlobalFree(message);
    }
  }
  return S_OK;
}

static HRESULT AddScriptToExecuteOnDocumentCreated_Invoke(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler * This, HRESULT errorCode, LPCWSTR id) {
  webview2 *pwv2 = WEBVIEW2_PTR_FROM(This, add_script_on_document_created_handler);
  webview_print_log("AddScriptToExecuteOnDocumentCreated");
  pwv2->ready = 1;
  PostMessageW(pwv2->hwnd, WM_WEBVIEW_READY, 0, 0);
  return S_OK;
}

static HRESULT CreateWebView2Controller_Invoke(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler * This, HRESULT result, ICoreWebView2Controller *webViewController) {
  webview_print_log("CreateWebView2Controller_Invoke()");
  ICoreWebView2 *webview;
  if (FAILED(result) || (webViewController == NULL)) {
    char buffer[256];
    sprintf(buffer, "CreateWebView2Controller_Invoke(%p, %08X, %p) => %08X (fail)", This, (int)result, webViewController, (int)E_FAIL);
    webview_print_log(buffer);
    return E_FAIL;
  }
  webViewController->lpVtbl->AddRef(webViewController);

  webview2 *pwv2 = WEBVIEW2_PTR_FROM(This, controller_created_handler);
  pwv2->controller = webViewController;

  webViewController->lpVtbl->get_CoreWebView2(webViewController, &webview);
  pwv2->webview = webview;

  ICoreWebView2Settings* settings;
  webview->lpVtbl->get_Settings(webview, &settings);
  if (settings->lpVtbl != NULL) {
    settings->lpVtbl->put_AreDevToolsEnabled(settings, pwv2->debug);
    settings->lpVtbl->put_AreDefaultContextMenusEnabled(settings, pwv2->debug);
  }

  RECT bounds;
  GetClientRect(pwv2->hwnd, &bounds);
  webViewController->lpVtbl->put_Bounds(webViewController, bounds);

  webview->lpVtbl->AddScriptToExecuteOnDocumentCreated(webview, L"window.external={invoke:s=>window.chrome.webview.postMessage(s)}", &pwv2->add_script_on_document_created_handler);

  EventRegistrationToken token;
  webview->lpVtbl->add_WebMessageReceived(webview, &pwv2->message_received_event_handler, &token);

  if ((pwv2->url != NULL) && (strlen(pwv2->url) > 0)) {
    wchar_t *wurl = webview_to_utf16(pwv2->url);
    if (wurl != NULL) {
      webview_print_log("CreateWebView2Controller_Invoke() Navigate");
      webview->lpVtbl->Navigate(webview, wurl);
      GlobalFree(wurl);
    }
  }
  return S_OK;
}

static HRESULT CreateWebView2Environment_Invoke(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler * This, HRESULT result, ICoreWebView2Environment * webViewEnvironment) {
  webview_print_log("CreateWebView2Environment_Invoke()");
  if (FAILED(result) || (webViewEnvironment == NULL)) {
    char buffer[256];
    sprintf(buffer, "CreateWebView2Environment_Invoke(%p, %08X, %p) => %08X (fail)", This, (int)result, webViewEnvironment, (int)E_FAIL);
    webview_print_log(buffer);
    return E_FAIL;
  }
  webViewEnvironment->lpVtbl->AddRef(webViewEnvironment);
  webview2 *pwv2 = WEBVIEW2_PTR_FROM(This, env_created_handler);
  webview_print_log("CreateWebView2Environment_Invoke() CreateCoreWebView2Controller");
  webViewEnvironment->lpVtbl->CreateCoreWebView2Controller(webViewEnvironment, pwv2->hwnd, &pwv2->controller_created_handler);
  webview_print_log("CreateWebView2Environment_Invoke() => ok");
  return S_OK;
}

static HRESULT NoOpQueryInterface(void * This, REFIID riid, void **ppvObject) {
  return S_OK;
}
static ULONG NoOpAddRef(void * This) {
  return 1;
}
static ULONG NoOpRelease(void * This) {
  return 1;
}

static void InitWebView2(webview2 *pwv2) {
  pwv2->env_created_handler_vtbl.QueryInterface = (HRESULT (*)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler * This, REFIID riid, void **ppvObject)) &NoOpQueryInterface;
  pwv2->env_created_handler_vtbl.AddRef = (ULONG (*)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler * This)) &NoOpAddRef;
  pwv2->env_created_handler_vtbl.Release = (ULONG (*)(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler * This)) &NoOpRelease;
  pwv2->env_created_handler_vtbl.Invoke = &CreateWebView2Environment_Invoke;
  pwv2->env_created_handler.lpVtbl = &pwv2->env_created_handler_vtbl;
  pwv2->controller_created_handler_vtbl.QueryInterface = (HRESULT (*)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler * This, REFIID riid, void **ppvObject)) &NoOpQueryInterface;
  pwv2->controller_created_handler_vtbl.AddRef = (ULONG (*)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler * This)) &NoOpAddRef;
  pwv2->controller_created_handler_vtbl.Release = (ULONG (*)(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler * This)) &NoOpRelease;
  pwv2->controller_created_handler_vtbl.Invoke = &CreateWebView2Controller_Invoke;
  pwv2->controller_created_handler.lpVtbl = &pwv2->controller_created_handler_vtbl;
  pwv2->message_received_event_handler_vtbl.QueryInterface = (HRESULT (*)(ICoreWebView2WebMessageReceivedEventHandler * This, REFIID riid, void **ppvObject)) &NoOpQueryInterface;
  pwv2->message_received_event_handler_vtbl.AddRef = (ULONG (*)(ICoreWebView2WebMessageReceivedEventHandler * This)) &NoOpAddRef;
  pwv2->message_received_event_handler_vtbl.Release = (ULONG (*)(ICoreWebView2WebMessageReceivedEventHandler * This)) &NoOpRelease;
  pwv2->message_received_event_handler_vtbl.Invoke = &WebView2WebMessageReceivedEventHandleInvoke;
  pwv2->message_received_event_handler.lpVtbl = &pwv2->message_received_event_handler_vtbl;
  pwv2->add_script_on_document_created_handler_vtbl.QueryInterface = (HRESULT (*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler * This, REFIID riid, void **ppvObject)) &NoOpQueryInterface;
  pwv2->add_script_on_document_created_handler_vtbl.AddRef = (ULONG (*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler * This)) &NoOpAddRef;
  pwv2->add_script_on_document_created_handler_vtbl.Release = (ULONG (*)(ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler * This)) &NoOpRelease;
  pwv2->add_script_on_document_created_handler_vtbl.Invoke = &AddScriptToExecuteOnDocumentCreated_Invoke;
  pwv2->add_script_on_document_created_handler.lpVtbl = &pwv2->add_script_on_document_created_handler_vtbl;
}

WEBVIEW2_WIN32_API webview2 * CreateWebView2(HWND hwnd, const char *url, int debug) {
  WCHAR dirname[MAX_PATH];
  webview2 *pwv2 = NULL;
  if (CreateCoreWebView2EnvironmentFn != NULL) {
    pwv2 = (webview2 *)GlobalAlloc(GMEM_FIXED, sizeof(webview2));
    if (pwv2 != NULL) {
      InitWebView2(pwv2);
      pwv2->ready = 0;
      pwv2->jsToEval = NULL;
      pwv2->webview = NULL;
      pwv2->hwnd = hwnd;
      pwv2->url = url;
      pwv2->debug = debug;
      PCWSTR userDataFolder = getUserData(dirname, MAX_PATH);
      HRESULT hr = CreateCoreWebView2EnvironmentFn(NULL, userDataFolder, NULL, &pwv2->env_created_handler);
      if (FAILED(hr)) {
        char buffer[256];
        sprintf(buffer, CREATE_COREWEBVIEW2_ENVIRONMENTWITHOPTIONS_FN_NAME "(%p, %p, %p) => %08X (fail)", NULL, userDataFolder, NULL, (int)hr);
        webview_print_log(buffer);
        GlobalFree(pwv2);
        pwv2 = NULL;
      }
    }
  } else {
    webview_print_log("CreateWebView2 error WebView2 is not enabled");
  }
  return pwv2;
}

WEBVIEW2_WIN32_API void ReleaseWebView2(webview2 *pwv2) {
  if (pwv2 != NULL) {
    GlobalFree(pwv2);
  }
}

WEBVIEW2_WIN32_API void WebView2SetBounds(webview2 *pwv2, RECT bounds) {
  if ((pwv2 != NULL) && (pwv2->controller != NULL) && (pwv2->webview != NULL)) {
    pwv2->controller->lpVtbl->put_Bounds(pwv2->controller, bounds);
  }
}

WEBVIEW2_WIN32_API void WebView2RegisterCallback(webview2 *pwv2, WebView2CallbackFn cb, void *context) {
  if (pwv2 != NULL) {
    pwv2->cb = cb;
    pwv2->context = context;
  }
}

static char * formatJavaScriptForEval(const char *js) {
  static const char *prologue = "(function(){";
  static const char *epilogue = ";})();";
  int n = strlen(prologue) + strlen(epilogue) + strlen(js) + 1;
  char *fjs = (char *)malloc(n);
  if (fjs != NULL) {
    snprintf(fjs, n, "%s%s%s", prologue, js, epilogue);
  }
  return fjs;
}

WEBVIEW2_WIN32_API int WebView2Eval(webview2 *pwv2, const char *js) {
  if (pwv2->webview != NULL) {
    char *fjs = formatJavaScriptForEval(js);
    if (fjs != NULL) {
      wchar_t *wjs = webview_to_utf16(fjs);
      free(fjs);
      if (wjs != NULL) {
        ICoreWebView2ExecuteScriptCompletedHandler *handler = NULL;
        pwv2->webview->lpVtbl->ExecuteScript(pwv2->webview, wjs, handler);
        GlobalFree(wjs);
        return 0;
      }
    }
  }
  webview_print_log("WebView2Eval() not available yet");
  return 1;
}

static int WebView2Enable() {
  TCHAR modulePath[MAX_PATH + 22];
  webview_print_log("Loading WebView2Loader (1.0.818)");
  findWebView2BrowserExecutableFolder();
  getWebView2LoaderFileName(modulePath);
  webview_print_log(modulePath);
  HMODULE hWebView2LoaderModule = LoadLibraryA(modulePath);
  if (hWebView2LoaderModule != NULL) {
    GetWebView2BrowserVersionInfoFnType GetWebView2BrowserVersionInfoFn = (GetWebView2BrowserVersionInfoFnType)GetProcAddress(hWebView2LoaderModule, GET_AVAILABLE_COREWEBVIEW2_BROWSER_VERSION_FN_NAME);
    CreateCoreWebView2EnvironmentFn = (CreateCoreWebView2EnvironmentWithOptionsFnType)GetProcAddress(hWebView2LoaderModule, CREATE_COREWEBVIEW2_ENVIRONMENTWITHOPTIONS_FN_NAME);
    if ((CreateCoreWebView2EnvironmentFn != NULL) && (GetWebView2BrowserVersionInfoFn != NULL)) {
      LPWSTR versionInfo = NULL;
      HRESULT hr = GetWebView2BrowserVersionInfoFn(NULL, &versionInfo);
      if ((hr == S_OK) && (versionInfo != NULL)) {
        webview_print_log("WebView2 enabled");
        OutputDebugStringW(versionInfo);
        return 1;
      } else {
        webview_print_log("Unable to get version");
      }
    } else {
      webview_print_log("Uncompatible");
    }
  } else {
    webview_print_log("Unable to load, you could set " WEBVIEW2_WIN32_PATH);
  }
  CreateCoreWebView2EnvironmentFn = NULL;
  webview_print_log("Not available");
  return 0;
}

static int webview_webview2_enabled = 0;

static void WebView2Callback(webview2 *wv, const char *message, void *context) {
  struct webview *w = (struct webview *)context;
  if (w != NULL) {
    if (w->external_invoke_cb != NULL) {
      w->external_invoke_cb(w, message);
    }
  }
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                LPARAM lParam) {
  struct webview *w = (struct webview *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  RECT rect;
  switch (uMsg) {
  case WM_CREATE:
    w = (struct webview *)((CREATESTRUCT *)lParam)->lpCreateParams;
    w->priv->hwnd = hwnd;
    if (webview_webview2_enabled) {
      w->priv->webview2 = CreateWebView2(hwnd, w->url, w->debug);
      if (w->priv->webview2 != NULL) {
        WebView2RegisterCallback(w->priv->webview2, &WebView2Callback, w);
      }
      return TRUE;
    }
    return EmbedBrowserObject(w);
  case WM_DESTROY:
    if (webview_webview2_enabled) {
      ReleaseWebView2(w->priv->webview2);
      w->priv->webview2 = NULL;
    } else {
      UnEmbedBrowserObject(w);
    }
    PostQuitMessage(0);
    return TRUE;
  case WM_SIZE:
    GetClientRect(hwnd, &rect);
    if (webview_webview2_enabled) {
      WebView2SetBounds(w->priv->webview2, rect);
    } else {
      IWebBrowser2 *webBrowser2;
      IOleObject *browser = *w->priv->browser;
      if (browser->lpVtbl->QueryInterface(browser, iid_unref(&IID_IWebBrowser2),
                                          (void **)&webBrowser2) == S_OK) {
        webBrowser2->lpVtbl->put_Width(webBrowser2, rect.right);
        webBrowser2->lpVtbl->put_Height(webBrowser2, rect.bottom);
      }
    }
    return TRUE;
  case WM_WEBVIEW_READY: {
    webview_print_log("WebView ready");
    if (webview_webview2_enabled && (w->priv->webview2->jsToEval != NULL)) {
      char *js = w->priv->webview2->jsToEval;
      w->priv->webview2->jsToEval = NULL;
      webview_eval(w, js);
      free(js);
    }
    return TRUE;
  }
  case WM_WEBVIEW_DISPATCH: {
    webview_dispatch_fn f = (webview_dispatch_fn)wParam;
    void *arg = (void *)lParam;
    (*f)(w, arg);
    return TRUE;
  }
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#define WEBVIEW_KEY_FEATURE_BROWSER_EMULATION                                  \
  "Software\\Microsoft\\Internet "                                             \
  "Explorer\\Main\\FeatureControl\\FEATURE_BROWSER_EMULATION"

static int webview_fix_ie_compat_mode() {
  HKEY hKey;
  DWORD ie_version = 11000;
  TCHAR appname[MAX_PATH + 1];
  TCHAR *p;
  if (GetModuleFileName(NULL, appname, MAX_PATH + 1) == 0) {
    return -1;
  }
  for (p = &appname[strlen(appname) - 1]; p != appname && *p != '\\'; p--) {
  }
  p++;
  if (RegCreateKey(HKEY_CURRENT_USER, WEBVIEW_KEY_FEATURE_BROWSER_EMULATION,
                   &hKey) != ERROR_SUCCESS) {
    return -1;
  }
  if (RegSetValueEx(hKey, p, 0, REG_DWORD, (BYTE *)&ie_version,
                    sizeof(ie_version)) != ERROR_SUCCESS) {
    RegCloseKey(hKey);
    return -1;
  }
  RegCloseKey(hKey);
  return 0;
}

WEBVIEW_API int webview_init(struct webview *w) {
  WNDCLASSEX wc;
  HINSTANCE hInstance;
  DWORD style;
  RECT clientRect;
  RECT rect;

  webview_webview2_enabled = WebView2Enable();

  if (webview_fix_ie_compat_mode() < 0) {
    return -1;
  }

  hInstance = GetModuleHandle(NULL);
  if (hInstance == NULL) {
    return -1;
  }
  if (OleInitialize(NULL) != S_OK) {
    return -1;
  }
  ZeroMemory(&wc, sizeof(WNDCLASSEX));
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.hInstance = hInstance;
  wc.lpfnWndProc = wndproc;
  wc.lpszClassName = classname;
  RegisterClassEx(&wc);

  style = WS_OVERLAPPEDWINDOW;
  if (!w->resizable) {
    style = WS_OVERLAPPED | WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU;
  }

  rect.left = 0;
  rect.top = 0;
  rect.right = w->width;
  rect.bottom = w->height;
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, 0);

  GetClientRect(GetDesktopWindow(), &clientRect);
  int left = (clientRect.right / 2) - ((rect.right - rect.left) / 2);
  int top = (clientRect.bottom / 2) - ((rect.bottom - rect.top) / 2);
  rect.right = rect.right - rect.left + left;
  rect.left = left;
  rect.bottom = rect.bottom - rect.top + top;
  rect.top = top;

  w->priv = (struct webview_priv *)malloc(sizeof(struct webview_priv));
  if (!w->priv) {
    return -1;
  }
  memset(w->priv, 0, sizeof(struct webview_priv));
  w->priv->hwnd =
      CreateWindowEx(0, classname, w->title, style, rect.left, rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     HWND_DESKTOP, NULL, hInstance, (void *)w);
  if (w->priv->hwnd == 0) {
    OleUninitialize();
    return -1;
  }

  SetWindowLongPtr(w->priv->hwnd, GWLP_USERDATA, (LONG_PTR)w);

  if (!webview_webview2_enabled) {
    DisplayHTMLPage(w);
  }

  SetWindowText(w->priv->hwnd, w->title);
  ShowWindow(w->priv->hwnd, SW_SHOWDEFAULT);
  UpdateWindow(w->priv->hwnd);
  SetFocus(w->priv->hwnd);

  return 0;
}

WEBVIEW_API int webview_loop(struct webview *w, int blocking) {
  MSG msg;
  if (blocking) {
    if (GetMessage(&msg, 0, 0, 0)<0) return 0;
  } else {
    if (!PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) return 0;
  }
  switch (msg.message) {
  case WM_QUIT:
    return -1;
  case WM_COMMAND:
  case WM_KEYDOWN:
  case WM_KEYUP: 
    if (!webview_webview2_enabled) {
      HRESULT r = S_OK;
      IWebBrowser2 *webBrowser2;
      IOleObject *browser = *w->priv->browser;
      if (browser->lpVtbl->QueryInterface(browser, iid_unref(&IID_IWebBrowser2),
                                          (void **)&webBrowser2) == S_OK) {
        IOleInPlaceActiveObject *pIOIPAO;
        if (browser->lpVtbl->QueryInterface(
                browser, iid_unref(&IID_IOleInPlaceActiveObject),
                (void **)&pIOIPAO) == S_OK) {
          r = pIOIPAO->lpVtbl->TranslateAccelerator(pIOIPAO, &msg);
          pIOIPAO->lpVtbl->Release(pIOIPAO);
        }
        webBrowser2->lpVtbl->Release(webBrowser2);
      }
      if (r != S_FALSE) {
        return 0;
      }
    }
    break;
  }
  TranslateMessage(&msg);
  DispatchMessage(&msg);
  return 0;
}

WEBVIEW_API int webview_eval(struct webview *w, const char *js) {
  if (webview_webview2_enabled) {
    webview2 *pwv2 = w->priv->webview2;
    if (pwv2->ready) {
      return WebView2Eval(pwv2, js);
    }
    char *jsToEval = pwv2->jsToEval;
    webview_print_log("defer eval as webview2 not ready");
    if (jsToEval == NULL) {
      int n = strlen(js) + 1;
      char *newJsToEval = (char *)malloc(n);
      strncpy(newJsToEval, js, n);
      pwv2->jsToEval = newJsToEval;
    } else {
      int n = strlen(jsToEval) + 1 + strlen(js) + 1;
      char *newJsToEval = (char *)malloc(n);
      snprintf(newJsToEval, n, "%s\n%s", jsToEval, js);
      free(jsToEval);
      pwv2->jsToEval = newJsToEval;
    }
    return 0;
  }
  IWebBrowser2 *webBrowser2;
  IHTMLDocument2 *htmlDoc2;
  IDispatch *docDispatch;
  IDispatch *scriptDispatch;
  if ((*w->priv->browser)
          ->lpVtbl->QueryInterface((*w->priv->browser),
                                   iid_unref(&IID_IWebBrowser2),
                                   (void **)&webBrowser2) != S_OK) {
    return -1;
  }

  if (webBrowser2->lpVtbl->get_Document(webBrowser2, &docDispatch) != S_OK) {
    return -1;
  }
  if (docDispatch->lpVtbl->QueryInterface(docDispatch,
                                          iid_unref(&IID_IHTMLDocument2),
                                          (void **)&htmlDoc2) != S_OK) {
    return -1;
  }
  if (htmlDoc2->lpVtbl->get_Script(htmlDoc2, &scriptDispatch) != S_OK) {
    return -1;
  }
  DISPID dispid;
  wchar_t *evalStr = L"eval";
  if (scriptDispatch->lpVtbl->GetIDsOfNames(
          scriptDispatch, iid_unref(&IID_NULL), &evalStr, 1,
          LOCALE_SYSTEM_DEFAULT, &dispid) != S_OK) {
    return -1;
  }

  DISPPARAMS params;
  VARIANT arg;
  VARIANT result;
  EXCEPINFO excepInfo;
  UINT nArgErr = (UINT)-1;
  params.cArgs = 1;
  params.cNamedArgs = 0;
  params.rgvarg = &arg;
  arg.vt = VT_BSTR;
  static const char *prologue = "(function(){";
  static const char *epilogue = ";})();";
  int n = strlen(prologue) + strlen(epilogue) + strlen(js) + 1;
  char *eval = (char *)malloc(n);
  snprintf(eval, n, "%s%s%s", prologue, js, epilogue);
  wchar_t *buf = webview_to_utf16(eval);
  if (buf == NULL) {
    return -1;
  }
  arg.bstrVal = SysAllocString(buf);
  if (scriptDispatch->lpVtbl->Invoke(
          scriptDispatch, dispid, iid_unref(&IID_NULL), 0, DISPATCH_METHOD,
          &params, &result, &excepInfo, &nArgErr) != S_OK) {
    return -1;
  }
  SysFreeString(arg.bstrVal);
  free(eval);
  scriptDispatch->lpVtbl->Release(scriptDispatch);
  htmlDoc2->lpVtbl->Release(htmlDoc2);
  docDispatch->lpVtbl->Release(docDispatch);
  return 0;
}

WEBVIEW_API void webview_dispatch(struct webview *w, webview_dispatch_fn fn,
                                  void *arg) {
  PostMessageW(w->priv->hwnd, WM_WEBVIEW_DISPATCH, (WPARAM)fn, (LPARAM)arg);
}

WEBVIEW_API void webview_set_title(struct webview *w, const char *title) {
  SetWindowText(w->priv->hwnd, title);
}

WEBVIEW_API void webview_set_fullscreen(struct webview *w, int fullscreen) {
  if (w->priv->is_fullscreen == !!fullscreen) {
    return;
  }
  if (w->priv->is_fullscreen == 0) {
    w->priv->saved_style = GetWindowLong(w->priv->hwnd, GWL_STYLE);
    w->priv->saved_ex_style = GetWindowLong(w->priv->hwnd, GWL_EXSTYLE);
    GetWindowRect(w->priv->hwnd, &w->priv->saved_rect);
  }
  w->priv->is_fullscreen = !!fullscreen;
  if (fullscreen) {
    MONITORINFO monitor_info;
    SetWindowLong(w->priv->hwnd, GWL_STYLE,
                  w->priv->saved_style & ~(WS_CAPTION | WS_THICKFRAME));
    SetWindowLong(w->priv->hwnd, GWL_EXSTYLE,
                  w->priv->saved_ex_style &
                      ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                        WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
    monitor_info.cbSize = sizeof(monitor_info);
    GetMonitorInfo(MonitorFromWindow(w->priv->hwnd, MONITOR_DEFAULTTONEAREST),
                   &monitor_info);
    RECT r;
    r.left = monitor_info.rcMonitor.left;
    r.top = monitor_info.rcMonitor.top;
    r.right = monitor_info.rcMonitor.right;
    r.bottom = monitor_info.rcMonitor.bottom;
    SetWindowPos(w->priv->hwnd, NULL, r.left, r.top, r.right - r.left,
                 r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  } else {
    SetWindowLong(w->priv->hwnd, GWL_STYLE, w->priv->saved_style);
    SetWindowLong(w->priv->hwnd, GWL_EXSTYLE, w->priv->saved_ex_style);
    SetWindowPos(w->priv->hwnd, NULL, w->priv->saved_rect.left,
                 w->priv->saved_rect.top,
                 w->priv->saved_rect.right - w->priv->saved_rect.left,
                 w->priv->saved_rect.bottom - w->priv->saved_rect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
}

WEBVIEW_API void webview_set_color(struct webview *w, uint8_t r, uint8_t g,
                                   uint8_t b, uint8_t a) {
  HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
  SetClassLongPtr(w->priv->hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)brush);
}

/* These are missing parts from MinGW */
#ifndef __IFileDialog_INTERFACE_DEFINED__
#define __IFileDialog_INTERFACE_DEFINED__
enum _FILEOPENDIALOGOPTIONS {
  FOS_OVERWRITEPROMPT = 0x2,
  FOS_STRICTFILETYPES = 0x4,
  FOS_NOCHANGEDIR = 0x8,
  FOS_PICKFOLDERS = 0x20,
  FOS_FORCEFILESYSTEM = 0x40,
  FOS_ALLNONSTORAGEITEMS = 0x80,
  FOS_NOVALIDATE = 0x100,
  FOS_ALLOWMULTISELECT = 0x200,
  FOS_PATHMUSTEXIST = 0x800,
  FOS_FILEMUSTEXIST = 0x1000,
  FOS_CREATEPROMPT = 0x2000,
  FOS_SHAREAWARE = 0x4000,
  FOS_NOREADONLYRETURN = 0x8000,
  FOS_NOTESTFILECREATE = 0x10000,
  FOS_HIDEMRUPLACES = 0x20000,
  FOS_HIDEPINNEDPLACES = 0x40000,
  FOS_NODEREFERENCELINKS = 0x100000,
  FOS_DONTADDTORECENT = 0x2000000,
  FOS_FORCESHOWHIDDEN = 0x10000000,
  FOS_DEFAULTNOMINIMODE = 0x20000000,
  FOS_FORCEPREVIEWPANEON = 0x40000000
};
typedef DWORD FILEOPENDIALOGOPTIONS;
typedef enum FDAP { FDAP_BOTTOM = 0, FDAP_TOP = 1 } FDAP;
DEFINE_GUID(IID_IFileDialog, 0x42f85136, 0xdb7e, 0x439c, 0x85, 0xf1, 0xe4, 0x07,
            0x5d, 0x13, 0x5f, 0xc8);
typedef struct IFileDialogVtbl {
  BEGIN_INTERFACE
  HRESULT(STDMETHODCALLTYPE *QueryInterface)
  (IFileDialog *This, REFIID riid, void **ppvObject);
  ULONG(STDMETHODCALLTYPE *AddRef)(IFileDialog *This);
  ULONG(STDMETHODCALLTYPE *Release)(IFileDialog *This);
  HRESULT(STDMETHODCALLTYPE *Show)(IFileDialog *This, HWND hwndOwner);
  HRESULT(STDMETHODCALLTYPE *SetFileTypes)
  (IFileDialog *This, UINT cFileTypes, const COMDLG_FILTERSPEC *rgFilterSpec);
  HRESULT(STDMETHODCALLTYPE *SetFileTypeIndex)
  (IFileDialog *This, UINT iFileType);
  HRESULT(STDMETHODCALLTYPE *GetFileTypeIndex)
  (IFileDialog *This, UINT *piFileType);
  HRESULT(STDMETHODCALLTYPE *Advise)
  (IFileDialog *This, IFileDialogEvents *pfde, DWORD *pdwCookie);
  HRESULT(STDMETHODCALLTYPE *Unadvise)(IFileDialog *This, DWORD dwCookie);
  HRESULT(STDMETHODCALLTYPE *SetOptions)
  (IFileDialog *This, FILEOPENDIALOGOPTIONS fos);
  HRESULT(STDMETHODCALLTYPE *GetOptions)
  (IFileDialog *This, FILEOPENDIALOGOPTIONS *pfos);
  HRESULT(STDMETHODCALLTYPE *SetDefaultFolder)
  (IFileDialog *This, IShellItem *psi);
  HRESULT(STDMETHODCALLTYPE *SetFolder)(IFileDialog *This, IShellItem *psi);
  HRESULT(STDMETHODCALLTYPE *GetFolder)(IFileDialog *This, IShellItem **ppsi);
  HRESULT(STDMETHODCALLTYPE *GetCurrentSelection)
  (IFileDialog *This, IShellItem **ppsi);
  HRESULT(STDMETHODCALLTYPE *SetFileName)(IFileDialog *This, LPCWSTR pszName);
  HRESULT(STDMETHODCALLTYPE *GetFileName)(IFileDialog *This, LPWSTR *pszName);
  HRESULT(STDMETHODCALLTYPE *SetTitle)(IFileDialog *This, LPCWSTR pszTitle);
  HRESULT(STDMETHODCALLTYPE *SetOkButtonLabel)
  (IFileDialog *This, LPCWSTR pszText);
  HRESULT(STDMETHODCALLTYPE *SetFileNameLabel)
  (IFileDialog *This, LPCWSTR pszLabel);
  HRESULT(STDMETHODCALLTYPE *GetResult)(IFileDialog *This, IShellItem **ppsi);
  HRESULT(STDMETHODCALLTYPE *AddPlace)
  (IFileDialog *This, IShellItem *psi, FDAP fdap);
  HRESULT(STDMETHODCALLTYPE *SetDefaultExtension)
  (IFileDialog *This, LPCWSTR pszDefaultExtension);
  HRESULT(STDMETHODCALLTYPE *Close)(IFileDialog *This, HRESULT hr);
  HRESULT(STDMETHODCALLTYPE *SetClientGuid)(IFileDialog *This, REFGUID guid);
  HRESULT(STDMETHODCALLTYPE *ClearClientData)(IFileDialog *This);
  HRESULT(STDMETHODCALLTYPE *SetFilter)
  (IFileDialog *This, IShellItemFilter *pFilter);
  END_INTERFACE
} IFileDialogVtbl;
interface IFileDialog {
  CONST_VTBL IFileDialogVtbl *lpVtbl;
};
DEFINE_GUID(IID_IFileOpenDialog, 0xd57c7288, 0xd4ad, 0x4768, 0xbe, 0x02, 0x9d,
            0x96, 0x95, 0x32, 0xd9, 0x60);
DEFINE_GUID(IID_IFileSaveDialog, 0x84bccd23, 0x5fde, 0x4cdb, 0xae, 0xa4, 0xaf,
            0x64, 0xb8, 0x3d, 0x78, 0xab);
#endif

WEBVIEW_API void webview_dialog(struct webview *w,
                                enum webview_dialog_type dlgtype, int flags,
                                const char *title, const char *arg,
                                char *result, size_t resultsz) {
  if (dlgtype == WEBVIEW_DIALOG_TYPE_OPEN ||
      dlgtype == WEBVIEW_DIALOG_TYPE_SAVE) {
    IFileDialog *dlg = NULL;
    IShellItem *res = NULL;
    WCHAR *ws = NULL;
    char *s = NULL;
    FILEOPENDIALOGOPTIONS opts = 0, add_opts = 0;
    if (dlgtype == WEBVIEW_DIALOG_TYPE_OPEN) {
      if (CoCreateInstance(
              iid_unref(&CLSID_FileOpenDialog), NULL, CLSCTX_INPROC_SERVER,
              iid_unref(&IID_IFileOpenDialog), (void **)&dlg) != S_OK) {
        goto error_dlg;
      }
      if (flags & WEBVIEW_DIALOG_FLAG_DIRECTORY) {
        add_opts |= FOS_PICKFOLDERS;
      }
      add_opts |= FOS_NOCHANGEDIR | FOS_ALLNONSTORAGEITEMS | FOS_NOVALIDATE |
                  FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_SHAREAWARE |
                  FOS_NOTESTFILECREATE | FOS_NODEREFERENCELINKS |
                  FOS_FORCESHOWHIDDEN | FOS_DEFAULTNOMINIMODE;
    } else {
      if (CoCreateInstance(
              iid_unref(&CLSID_FileSaveDialog), NULL, CLSCTX_INPROC_SERVER,
              iid_unref(&IID_IFileSaveDialog), (void **)&dlg) != S_OK) {
        goto error_dlg;
      }
      add_opts |= FOS_OVERWRITEPROMPT | FOS_NOCHANGEDIR |
                  FOS_ALLNONSTORAGEITEMS | FOS_NOVALIDATE | FOS_SHAREAWARE |
                  FOS_NOTESTFILECREATE | FOS_NODEREFERENCELINKS |
                  FOS_FORCESHOWHIDDEN | FOS_DEFAULTNOMINIMODE;
    }
    if (dlg->lpVtbl->GetOptions(dlg, &opts) != S_OK) {
      goto error_dlg;
    }
    opts &= ~FOS_NOREADONLYRETURN;
    opts |= add_opts;
    if (dlg->lpVtbl->SetOptions(dlg, opts) != S_OK) {
      goto error_dlg;
    }
    if (dlg->lpVtbl->Show(dlg, w->priv->hwnd) != S_OK) {
      goto error_dlg;
    }
    if (dlg->lpVtbl->GetResult(dlg, &res) != S_OK) {
      goto error_dlg;
    }
    if (res->lpVtbl->GetDisplayName(res, SIGDN_FILESYSPATH, &ws) != S_OK) {
      goto error_result;
    }
    s = webview_from_utf16(ws);
    strncpy(result, s, resultsz);
    result[resultsz - 1] = '\0';
    CoTaskMemFree(ws);
  error_result:
    res->lpVtbl->Release(res);
  error_dlg:
    dlg->lpVtbl->Release(dlg);
    return;
  } else if (dlgtype == WEBVIEW_DIALOG_TYPE_ALERT) {
#if 0
    /* MinGW often doesn't contain TaskDialog, we'll use MessageBox for now */
    WCHAR *wtitle = webview_to_utf16(title);
    WCHAR *warg = webview_to_utf16(arg);
    TaskDialog(w->priv->hwnd, NULL, NULL, wtitle, warg, 0, NULL, NULL);
    GlobalFree(warg);
    GlobalFree(wtitle);
#else
    UINT type = MB_OK;
    switch (flags & WEBVIEW_DIALOG_FLAG_ALERT_MASK) {
    case WEBVIEW_DIALOG_FLAG_INFO:
      type |= MB_ICONINFORMATION;
      break;
    case WEBVIEW_DIALOG_FLAG_WARNING:
      type |= MB_ICONWARNING;
      break;
    case WEBVIEW_DIALOG_FLAG_ERROR:
      type |= MB_ICONERROR;
      break;
    }
    MessageBox(w->priv->hwnd, arg, title, type);
#endif
  }
}

WEBVIEW_API void webview_terminate(struct webview *w) { PostQuitMessage(0); }

WEBVIEW_API void webview_exit(struct webview *w) {
  DestroyWindow(w->priv->hwnd);
  OleUninitialize();
  free(w->priv);
}

WEBVIEW_API void webview_print_log(const char *s) { OutputDebugString(s); }

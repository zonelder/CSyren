#ifndef __CSYREN_DX_LOG__
#define __CSYREN_DX_LOG__

#include <d3d12.h>
#include <format>
#include "cstdmf/log.h"

namespace csyren::log
{
    inline HRESULT log_hr(const char* expr,HRESULT hr)
    {
        if (FAILED(hr))
            csyren::log::error("{} failed with HRESULT {:#x}", expr, static_cast<unsigned>(hr));
        return hr;
    }
}

#define DX_LOG(expr) ::csyren::log::log_hr(#expr, (expr))
#define DX_SUCCEEDED(expr) SUCCEEDED(DX_LOG(expr))
#define DX_FAILED(expr) FAILED(DX_LOG(expr))


#endif

#include "pch.h"
#include "dispatch_handle.h"
#include "window.h"
#include "services.h"

namespace csyren::core::details
{
	void DispatchHandle::setToMainWindow()
	{
		Services::get<Window>()->setDispatchHandle(this);
	}
}
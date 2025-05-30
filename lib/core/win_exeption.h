#ifndef __CSYREN_WIN_EXEPTION__
#define __CSYREN_WIN_EXEPTION__

#include <Windows>
#include <ostream>

namespace csyren::exeption
{
	namespace
	{
		class PointedException
		{
		public:
			PointedException(int line, const char* file) noexcept
				_line(line),
				_file(file)
			{
			}
		protected:
			int _line;
			std::string _file;
		};
	}

	class HrException: public PointedException
	{
	public:
		HrException(DWORD errorCode, const char* file, int line) noexcept :
			PointedException(line, file),
			_code(errorCode),
			_report(formReport())
		{

		}
		const char* what() const noexcept
		{
			return _report;
		}

	private:
		std::string formReport()
		{
			std::ostringstream oss;
			oss << type_str << std::endl
				<< "[ERROR CODE]" << _code << std::endl
				<< "[DESCRIPTION]" << getErrorDscription() << std::endl
				<< getOriginalString();
			p_buffer = oss.str();
			return p_buffer.c_str();
		}

		static const char* type_str = "Windows Exeption";
		DWORD _code;
		std::string _report;
	};
}




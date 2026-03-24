#pragma once
#include <string>

#define WIN32_LEAN_AND_MEAN
//#define NOMINMAX
#include <Windows.h>
#include "log.h"

namespace csyren::cstdmf
{
	/**
 * @brief Converts a wide string (std::wstring, UTF-16 on Windows) to a UTF-8 encoded string (std::string).
 *
 * This function uses the recommended Windows API 'WideCharToMultiByte' for safe and efficient conversion.
 * It is the standard way to interface with Windows APIs that return wide strings.
 *
 * @param w The wide string to convert.
 * @return A UTF-8 encoded string.
 * @throws std::runtime_error if the conversion fails.
 */
	inline std::string to_string(const std::wstring& w)
	{
		if (w.empty()) return std::string();

		// 1. Calculate the required buffer size.
		// The last parameter is -1, which tells the function to process the entire null-terminated string.
		int size_needed = WideCharToMultiByte(CP_UTF8, 0, &w[0], (int)w.size(), NULL, 0, NULL, NULL);
		if (size_needed == 0)
		{
			log::error("WideCharToMultiByte failed to calculate buffer size.");
			return std::string();
		}

		// 2. Allocate the string and perform the conversion.
		std::string s(size_needed, 0);
		int chars_converted = WideCharToMultiByte(CP_UTF8, 0, &w[0], (int)w.size(), &s[0], size_needed, NULL, NULL);
		if (chars_converted == 0)
		{
			log::error("WideCharToMultiByte failed to convert string.");
			return std::string();
		}

		return s;
	}

	/**
	 * @brief Converts a UTF-8 encoded string (std::string) to a wide string (std::wstring).
	 *
	 * This function uses the recommended Windows API 'MultiByteToWideChar' for safe and efficient conversion.
	 * It is the standard way to pass strings from your engine to Windows APIs that expect wide strings.
	 *
	 * @param s The UTF-8 encoded string to convert.
	 * @return A wide string (UTF-16 on Windows).
	 * @throws std::runtime_error if the conversion fails.
	 */
	inline std::wstring to_wstring(const std::string& s)
	{
		if (s.empty()) return std::wstring();

		// 1. Calculate the required buffer size.
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, &s[0], (int)s.size(), NULL, 0);
		if (size_needed == 0)
		{
			log::error("MultiByteToWideChar failed to calculate buffer size.");
			return std::wstring();
		}

		// 2. Allocate the wide string and perform the conversion.
		std::wstring w(size_needed, 0);
		int chars_converted = MultiByteToWideChar(CP_UTF8, 0, &s[0], (int)s.size(), &w[0], size_needed);
		if (chars_converted == 0)
		{
			log::error("MultiByteToWideChar failed to convert string.");
			return std::wstring();
		}

		return w;
	}

	/**
	 * @brief remove spaces from the start and the end of the string.
	 * 
	 * \param s
	 * \return  return new string without corner spaces
	 */
	inline std::string trim(const std::string& s)
	{
		size_t first = s.find_first_not_of(" \t\n\r");
		if (std::string::npos == first) {
			return s;
		}
		size_t last = s.find_last_not_of(" \t\n\r");
		return s.substr(first, (last - first + 1));
	}

	inline std::string loadStringFromFile(const std::string& filepath)
	{
		std::ifstream file(filepath);
		if (!file.is_open()) {
			return "";
		}
		return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	}

	inline std::string toUpper(std::string s) 
	{
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return std::toupper(c); });
		return s;
	}
}

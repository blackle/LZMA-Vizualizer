#pragma once

#include <iostream>
#include <cstdio>
#include <functional>

namespace realcolor
{
	namespace
	{
		constexpr int red(int rgb)
		{
			return (rgb >> 16) & 0xFF;
		}

		constexpr int green(int rgb)
		{
			return (rgb >> 8) & 0xFF;
		}

		constexpr int blue(int rgb)
		{
			return rgb & 0xFF;
		}
	};

	inline std::ostream& reset(std::ostream& stream)
	{
		stream << "\x1b[0m";
		return stream;
	}

	inline std::string fg(int r, int g, int b)
	{
		std::stringstream color;
		color << "\x1b[38;2;" << r << ";" << g << ";" << b << "m";
		return color.str();
	}

	inline std::string bg(int r, int g, int b)
	{
		std::stringstream color;
		color << "\x1b[48;2;" << r << ";" << g << ";" << b << "m";
		return color.str();
	}

	//don't be scared of this template magic, this is just so we can pass all floating point types in (floats, doubles, etc)
	template<typename floatType>
	inline typename std::enable_if<std::is_floating_point<floatType>::value, std::string>::type
	fg(floatType r, floatType g, floatType b)
	{
		return fg(static_cast<int>(r*0xFF), static_cast<int>(g*0xFF), static_cast<int>(b*0xFF));
	}

	template<typename floatType>
	inline typename std::enable_if<std::is_floating_point<floatType>::value, std::string>::type
	bg(floatType r, floatType g, floatType b)
	{
		return bg(static_cast<int>(r*0xFF), static_cast<int>(g*0xFF), static_cast<int>(b*0xFF));
	}

	inline std::string fg(int rgb)
	{
		return fg(red(rgb), green(rgb), blue(rgb));
	}

	inline std::string bg(int rgb)
	{
		return bg(red(rgb), green(rgb), blue(rgb));
	}
};

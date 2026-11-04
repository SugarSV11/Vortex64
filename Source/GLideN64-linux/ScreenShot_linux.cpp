// windows/ScreenShot.cpp for Linux: the captured frame (bottom-up RGB) as a PNG in the folder,
// named GLideN64_<rom>_NNN.png
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <wchar.h>
#include <windows/ScreenShot.h>

static std::string narrow(const std::wstring & _s)
{
	std::vector<char> buf(_s.size() * 4 + 1);
	const size_t n = wcstombs(buf.data(), _s.c_str(), buf.size() - 1);
	return n == (size_t)-1 ? std::string() : std::string(buf.data(), n);
}

static bool writePng(const std::string & _file, int _width, int _height, const unsigned char * _buffer)
{
	FILE * fp = fopen(_file.c_str(), "wb");
	if (fp == nullptr)
		return false;
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	png_infop info = png != nullptr ? png_create_info_struct(png) : nullptr;
	if (png == nullptr || info == nullptr || setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(fp);
		remove(_file.c_str());
		return false;
	}
	png_init_io(png, fp);
	png_set_IHDR(png, info, _width, _height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	png_write_info(png, info);
	// glReadPixels gives the bottom row first
	for (int y = _height - 1; y >= 0; --y)
		png_write_row(png, const_cast<png_bytep>(_buffer + static_cast<size_t>(y) * _width * 3));
	png_write_end(png, nullptr);
	png_destroy_write_struct(&png, &info);
	fclose(fp);
	return true;
}

void SaveScreenshot(const wchar_t * _folder, const char * _name, int _width, int _height, const unsigned char * _data)
{
	std::string folder = narrow(_folder);
	while (folder.size() > 1 && folder.back() == '/')
		folder.pop_back();
	mkdir(folder.c_str(), 0755);
	struct stat st;
	if (stat(folder.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
		return;

	std::string romName = _name;
	for (char & c : romName) {
		if (c == ' ')
			c = '_';
		else if (c == ':')
			c = ';';
		else if (c == '/')
			c = '-';
	}

	for (int i = 0; i < 1000; ++i) {
		char number[8];
		snprintf(number, sizeof(number), "%03i", i);
		const std::string file = folder + "/GLideN64_" + romName + "_" + number + ".png";
		if (stat(file.c_str(), &st) != 0) {
			writePng(file, _width, _height, _data);
			return;
		}
	}
}

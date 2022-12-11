#ifndef __BMP_H__
#define __BMP_H__

#include <stdio.h>
#include <stdlib.h>

typedef struct BMP_
{
	unsigned w;
	unsigned h;
	char *data;
} BMP;

static void create_bmp(BMP *self, unsigned w, unsigned h);
static int load_bmp(BMP *self, const char *path);
static void set_bmp_pixel(BMP *self, unsigned x, unsigned y, unsigned color);
static unsigned get_bmp_pixel(BMP *self, unsigned x, unsigned y);
static void save_bmp(BMP *self, const char *path);
static void release_bmp(BMP *self);

static void create_bmp(BMP *self, unsigned w, unsigned h)
{
	self->w = w;
	self->h = h;
	self->data = (char *)malloc(self->w * self->h * 3 + 24);
}

static int load_bmp(BMP *self, const char *path)
{
	self->w = 0;
	self->h = 0;
	self->data = 0;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	unsigned char header[54];
	if (!fread(header, 54, 1, f))
		return 0;
	self->w = (*(int *)(header + 18)) & 0xFFFFFFFF;
	self->h = (*(int *)(header + 22)) & 0xFFFFFFFF;
	char reversed = 0;
	int iw = (int)self->w, ih = (int)self->h;
	if (iw < 0)
	{
		self->w = (unsigned)(-iw);
		reversed = 1;
	}
	if (ih < 0)
	{
		self->h = (unsigned)(-ih);
		reversed = 1;
	}
	self->data = (char *)malloc(self->w * self->h * 3 + 24);
	int padlen = (4 - ((self->w * 3) % 4)) & 3;
	char padd[4] = {0, 0, 0, 0};
	int i = 0;
	int begin = reversed ? 0 : (self->h - 1);
	int end = reversed ? self->h : 0;
	int step = reversed ? 1 : -1;
	fseek(f, (int)(header[10]), SEEK_SET);
	for (i = begin; reversed ? i < end : i >= end; i += step)
	{
		if (!fread(self->data + (self->w * i * 3), self->w * 3, 1, f))
		{
			fclose(f);
			return 10;
		}
		if (padlen > 0)
		{
			if (!fread(padd, padlen, 1, f))
			{
				fclose(f);
				return 0;
			}
		}
	}
	fclose(f);
	return 1;
}

static void set_bmp_pixel(BMP *self, unsigned x, unsigned y, unsigned color)
{
	if (x >= self->w || y >= self->h)
		return;
	char rem = self->data[y * self->w * 3 + x * 3 + 3];
	color |= ((int)rem) << 24;
	*(unsigned *)&self->data[y * self->w * 3 + x * 3] = color;
}

static unsigned get_bmp_pixel(BMP *self, unsigned x, unsigned y)
{
	if (x >= self->w || y >= self->h)
		return 0;
	return (*(unsigned *)&self->data[y * self->w * 3 + x * 3]) & 0xFFFFFF;
}

static void save_bmp(BMP *self, const char *path)
{
	int w = self->w, h = self->h;
	char *data = self->data;
	unsigned char header[54] = {66, 77, 0, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	FILE *f = fopen(path, "wb+");
	fwrite(header, 54, 1, f);
	char padd[4] = {0, 0, 0, 0};
	int padlen = (4 - ((w * 3) % 4)) & 3;
	for (int y = h - 1; y >= 0; y--)
	{
		fwrite(data + y * w * 3, w * 3, 1, f);
		fwrite(padd, padlen, 1, f);
	}
	int size = ftell(f);
	fseek(f, 2, SEEK_SET);
	fwrite(&size, 1, 4, f);
	int raw = size - 54;
	fseek(f, 0x22, SEEK_SET);
	fwrite(&raw, 1, 4, f);
	fseek(f, 18, SEEK_SET);
	fwrite(&w, 1, 4, f);
	fwrite(&h, 1, 4, f);
	fclose(f);
}

static void release_bmp(BMP *self)
{
	free(self->data);
}

#endif
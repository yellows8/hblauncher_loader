#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <3ds.h>

#include "archive.h"

u32 extdata_archives_lowpathdata[TotalExtdataArchives][3];
FS_archive extdata_archives[TotalExtdataArchives];
u32 extdata_initialized = 0;

Result open_extdata()
{
	Result ret=0;
	u32 pos;
	u32 extdataID_homemenu, extdataID_theme;
	u8 region=0;

	ret = initCfgu();
	if(ret!=0)
	{
		printf("initCfgu() failed: 0x%08x\n", (unsigned int)ret);
		return ret;
	}

	ret = CFGU_SecureInfoGetRegion(&region);
	if(ret!=0)
	{
		printf("CFGU_SecureInfoGetRegion() failed: 0x%08x\n", (unsigned int)ret);
		return ret;
	}

	exitCfgu();

	if(region==1)//USA
	{
		extdataID_homemenu = 0x0000008f;
		extdataID_theme = 0x000002cd;
	}
	else if(region==2)//EUR
	{
		extdataID_homemenu = 0x00000098;
		extdataID_theme = 0x000002ce;
	}
	else//JPN/elsewhere
	{
		extdataID_homemenu = 0x00000082;
		extdataID_theme = 0x000002cc;
	}

	for(pos=0; pos<TotalExtdataArchives; pos++)
	{
		extdata_archives[pos].id = ARCH_EXTDATA;
		extdata_archives[pos].lowPath.type = PATH_BINARY;
		extdata_archives[pos].lowPath.size = 0xc;
		extdata_archives[pos].lowPath.data = (u8*)extdata_archives_lowpathdata[pos];

		memset(extdata_archives_lowpathdata[pos], 0, 0xc);
		extdata_archives_lowpathdata[pos][0] = 1;//mediatype, 1=SD
	}

	extdata_archives_lowpathdata[HomeMenu_Extdata][1] = extdataID_homemenu;//extdataID-low
	extdata_archives_lowpathdata[Theme_Extdata][1] = extdataID_theme;//extdataID-low

	ret = FSUSER_OpenArchive(NULL, &extdata_archives[HomeMenu_Extdata]);
	if(ret!=0)
	{
		printf("Failed to open homemenu extdata with extdataID=0x%08x, retval: 0x%08x\n", (unsigned int)extdataID_homemenu, (unsigned int)ret);
		return ret;
	}
	extdata_initialized |= 0x1;

	ret = FSUSER_OpenArchive(NULL, &extdata_archives[Theme_Extdata]);
	if(ret!=0)
	{
		printf("Failed to open theme extdata with extdataID=0x%08x, retval: 0x%08x\n", (unsigned int)extdataID_theme, (unsigned int)ret);
		printf("Exit this app, then goto Home Menu theme-settings so that Home Menu can create the theme extdata.\n");
		return ret;
	}
	extdata_initialized |= 0x2;

	return 0;
}

void close_extdata()
{
	u32 pos;

	for(pos=0; pos<TotalExtdataArchives; pos++)
	{
		if(extdata_initialized & (1<<pos))FSUSER_CloseArchive(NULL, &extdata_archives[pos]);
	}
}

Result archive_getfilesize(Archive archive, char *path, u32 *outsize)
{
	Result ret=0;
	struct stat filestats;
	u64 tmp64=0;
	Handle filehandle=0;

	char filepath[256];

	if(archive==SDArchive)
	{
		memset(filepath, 0, 256);
		strncpy(filepath, path, 255);

		if(stat(filepath, &filestats)==-1)return errno;

		*outsize = filestats.st_size;

		return 0;
	}

	ret = FSUSER_OpenFile(NULL, &filehandle, extdata_archives[archive], FS_makePath(PATH_CHAR, path), 1, 0);
	if(ret!=0)return ret;

	ret = FSFILE_GetSize(filehandle, &tmp64);
	if(ret==0)*outsize = (u32)tmp64;

	FSFILE_Close(filehandle);

	return ret;
}

Result archive_readfile(Archive archive, char *path, u8 *buffer, u32 size)
{
	Result ret=0;
	Handle filehandle=0;
	u32 tmpval=0;
	FILE *f;

	char filepath[256];

	if(archive==SDArchive)
	{
		memset(filepath, 0, 256);
		strncpy(filepath, path, 255);

		f = fopen(filepath, "r");
		if(f==NULL)return errno;

		tmpval = fread(buffer, 1, size, f);

		fclose(f);

		if(tmpval!=size)return -2;

		return 0;
	}

	ret = FSUSER_OpenFile(NULL, &filehandle, extdata_archives[archive], FS_makePath(PATH_CHAR, path), FS_OPEN_READ, 0);
	if(ret!=0)return ret;

	ret = FSFILE_Read(filehandle, &tmpval, 0, buffer, size);

	FSFILE_Close(filehandle);

	if(ret==0 && tmpval!=size)ret=-2;

	return ret;
}

Result archive_writefile(Archive archive, char *path, u8 *buffer, u32 size)
{
	Result ret=0;
	Handle filehandle=0;
	u32 tmpval=0;
	FILE *f;

	char filepath[256];

	if(archive==SDArchive)
	{
		memset(filepath, 0, 256);
		strncpy(filepath, path, 255);

		f = fopen(filepath, "w+");
		if(f==NULL)return errno;

		tmpval = fwrite(buffer, 1, size, f);

		fclose(f);

		if(tmpval!=size)return -2;

		return 0;
	}

	ret = FSUSER_OpenFile(NULL, &filehandle, extdata_archives[archive], FS_makePath(PATH_CHAR, path), FS_OPEN_WRITE, 0);
	if(ret!=0)return ret;

	ret = FSFILE_Write(filehandle, &tmpval, 0, buffer, size, FS_WRITE_FLUSH);

	FSFILE_Close(filehandle);

	if(ret==0 && tmpval!=size)ret=-2;

	return ret;
}

Result archive_copyfile(Archive inarchive, Archive outarchive, char *inpath, char *outpath, u8* buffer, u32 size, u32 maxbufsize, char *display_filepath)
{
	Result ret=0;
	u32 filesize=0;

	ret = archive_getfilesize(inarchive, inpath, &filesize);
	printf("archive_getfilesize() ret=0x%08x, size=0x%08x\n", (unsigned int)ret, (unsigned int)filesize);
	if(ret!=0)return ret;

	if(size==0 || size>filesize)
	{
		size = filesize;
	}

	if(size>maxbufsize)
	{
		printf("Size is too large.\n");
		ret = -1;
		return ret;
	}

	printf("Reading %s...\n", display_filepath);

	ret = archive_readfile(inarchive, inpath, buffer, size);
	if(ret!=0)
	{
		printf("Failed to read file: 0x%08x\n", (unsigned int)ret);
		return ret;
	}

	printf("Writing %s...\n", display_filepath);

	ret = archive_writefile(outarchive, outpath, buffer, size);
	if(ret!=0)
	{
		printf("Failed to write file: 0x%08x\n", (unsigned int)ret);
		return ret;
	}

	return ret;
}


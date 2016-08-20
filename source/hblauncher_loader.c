#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <3ds.h>

#include "builtin_rootca_der.h"

extern u32 PAYLOAD_TEXTADDR[];
extern u32 PAYLOAD_TEXTMAXSIZE;

extern Handle gspGpuHandle;

u8 *filebuffer = NULL;
u32 filebuffer_maxsize;

char regionids_table[7][4] = {//https://www.3dbrew.org/wiki/Nandrw/sys/SecureInfo_A
"JPN",
"USA",
"EUR",
"JPN", //"AUS"
"CHN",
"KOR",
"TWN"
};

void gxlowcmd_4(u32* inadr, u32* outadr, u32 size, u32 width0, u32 height0, u32 width1, u32 height1, u32 flags)
{
	GX_TextureCopy(inadr, width0 | (height0<<16), outadr, width1 | (height1<<16), size, flags);
}

Result http_getactual_payloadurl(char *requrl, char *outurl, u32 outurl_maxsize)
{
	Result ret=0;
	httpcContext context;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, requrl, 1);
	if(R_FAILED(ret))return ret;

	ret = httpcAddRequestHeaderField(&context, "User-Agent", "hblauncher_loader/"VERSION);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcAddTrustedRootCA(&context, (u8*)builtin_rootca_der, builtin_rootca_der_size);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcBeginRequest(&context);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	memset(outurl, 0, outurl_maxsize);
	ret = httpcGetResponseHeader(&context, "Location", outurl, outurl_maxsize);

	httpcCloseContext(&context);

	return ret;
}

Result http_download_payload(char *url, u32 *payloadsize)
{
	Result ret=0;
	u32 statuscode=0;
	u32 contentsize=0;
	httpcContext context;

	ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
	if(R_FAILED(ret))return ret;

	ret = httpcAddRequestHeaderField(&context, "User-Agent", "hblauncher_loader/"VERSION);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcAddTrustedRootCA(&context, (u8*)builtin_rootca_der, builtin_rootca_der_size);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcAddDefaultCert(&context, SSLC_DefaultRootCert_DigiCert_EV);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcBeginRequest(&context);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcGetResponseStatusCode(&context, &statuscode);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	if(statuscode!=200)
	{
		printf("Error: server returned HTTP statuscode %u.\n", (unsigned int)statuscode);
		httpcCloseContext(&context);
		return -2;
	}

	ret=httpcGetDownloadSizeState(&context, NULL, &contentsize);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	if(contentsize==0 || contentsize>PAYLOAD_TEXTMAXSIZE)
	{
		printf("Invalid HTTP content-size: 0x%08x.\n", (unsigned int)contentsize);
		ret = -3;
		httpcCloseContext(&context);
		return ret;
	}

	ret = httpcDownloadData(&context, filebuffer, contentsize, NULL);
	if(R_FAILED(ret))
	{
		httpcCloseContext(&context);
		return ret;
	}

	httpcCloseContext(&context);

	*payloadsize = contentsize;

	return 0;
}

Result loadsd_payload(char *filepath, u32 *payloadsize)
{
	struct stat filestats;
	FILE *f;
	size_t readsize=0;

	if(stat(filepath, &filestats)==-1)return errno;

	*payloadsize = filestats.st_size;

	if(filestats.st_size==0 || filestats.st_size>PAYLOAD_TEXTMAXSIZE)
	{
		printf("Invalid SD payload size: 0x%08x.\n", (unsigned int)filestats.st_size);
		return -3;
	}

	f = fopen(filepath, "r");
	if(f==NULL)return errno;

	readsize = fread(filebuffer, 1, filestats.st_size, f);
	fclose(f);

	if(readsize!=filestats.st_size)
	{
		printf("fread() failed with the SD payload.\n");
		return -2;
	}

	return 0;
}

Result savesd_payload(char *filepath, u32 payloadsize)
{
	FILE *f;
	size_t writesize=0;

	unlink(filepath);

	f = fopen(filepath, "w+");
	if(f==NULL)
	{
		printf("Failed to open the SD payload for writing.\n");
		return errno;
	}

	writesize = fwrite(filebuffer, 1, payloadsize, f);
	fclose(f);

	if(writesize!=payloadsize)
	{
		printf("fwrite() failed with the SD payload.\n");
		return -2;
	}

	return 0;
}

Result load_hblauncher()
{
	Result ret = 0;
	u8 region=0;
	bool new3dsflag = 0;

	OS_VersionBin nver_versionbin;
	OS_VersionBin cver_versionbin;

	u32 payloadsize = 0, payloadsize_aligned = 0;
	u32 payload_src = 0;

	u64 menu_programid = 0;
	AM_TitleEntry menu_title_entry;

	char payload_sysver[32];
	char payloadurl[0x80];
	char payload_sdpath[0x80];
	char tmpstr[256];

	void (*funcptr)(u32*, u32*) = NULL;
	u32 *paramblk = NULL;

	memset(&nver_versionbin, 0, sizeof(OS_VersionBin));
	memset(&cver_versionbin, 0, sizeof(OS_VersionBin));

	memset(payload_sysver, 0, sizeof(payload_sysver));
	memset(payloadurl, 0, sizeof(payloadurl));
	memset(payload_sdpath, 0, sizeof(payload_sdpath));
	memset(tmpstr, 0, sizeof(tmpstr));

	#ifdef VERBOSE
	printf("Getting system-info etc...\n");
	#endif

	ret = cfguInit();
	if(ret!=0)
	{
		printf("Failed to init cfgu: 0x%08x.\n", (unsigned int)ret);
		return ret;
	}
	ret = CFGU_SecureInfoGetRegion(&region);
	if(ret!=0)
	{
		printf("Failed to get region from cfgu: 0x%08x.\n", (unsigned int)ret);
		return ret;
	}
	if(region>=7)
	{
		printf("Region value from cfgu is invalid: 0x%02x.\n", (unsigned int)region);
		ret = -9;
		return ret;
	}
	cfguExit();

	APT_CheckNew3DS(&new3dsflag);

	ret = osGetSystemVersionData(&nver_versionbin, &cver_versionbin);
	if(ret!=0)
	{
		printf("Failed to load the system-version: 0x%08x.\n", (unsigned int)ret);
		return ret;
	}

	ret = amInit();
	if(ret==0)ret = APT_GetAppletInfo(APPID_HOMEMENU, &menu_programid, NULL, NULL, NULL, NULL);
	if(ret==0)ret = AM_GetTitleInfo(MEDIATYPE_NAND, 1, &menu_programid, &menu_title_entry);
	amExit();

	snprintf(payload_sysver, sizeof(payload_sysver)-1, "%s-%d-%d-%d-%d-%s", new3dsflag?"NEW":"OLD", cver_versionbin.mainver, cver_versionbin.minor, cver_versionbin.build, nver_versionbin.mainver, regionids_table[region]);
	snprintf(payloadurl, sizeof(payloadurl)-1, "https://smea.mtheall.com/get_payload.php?version=%s", payload_sysver);
	snprintf(payload_sdpath, sizeof(payload_sdpath)-1, "sdmc:/hblauncherloader/%s.bin", payload_sysver);

	if(ret==0)//Send the actual Home Menu title-version in the request URL when the menuver was loaded successfully. This is needed for when the title-version is different from what it should be with the current CVer. With this the server script will determine the Home Menu portion of the output URL using the input menuver instead of the system-version.
	{
		snprintf(tmpstr, sizeof(tmpstr)-1, "&menuver=%u", menu_title_entry.version);
		strncat(payloadurl, tmpstr, sizeof(payloadurl)-1);

		#ifdef VERBOSE
		printf("Using menuver: v%u.\n", menu_title_entry.version);
		#endif
	}

	mkdir("sdmc:/hblauncherloader", 0777);

	#ifdef VERBOSE
	printf("Detected system-version: %s %d.%d.%d-%d %s\n", new3dsflag?"New3DS":"Old3DS", cver_versionbin.mainver, cver_versionbin.minor, cver_versionbin.build, nver_versionbin.mainver, regionids_table[region]);
	#endif

	memset(filebuffer, 0, filebuffer_maxsize);

	hidScanInput();

	if((hidKeysHeld() & KEY_X) == 0)
	{
		#ifdef VERBOSE
		printf("Since the X button isn't pressed, this will now check for the otherapp payload on SD, with the following filepath: %s\n", payload_sdpath);
		#endif
		ret = loadsd_payload(payload_sdpath, &payloadsize);
	}
	else
	{
		#ifdef VERBOSE
		printf("Skipping SD payload load-attempt since the X button is pressed.\n");
		#endif
		ret = 1;
	}

	if(ret==0)
	{
		#ifdef VERBOSE
		printf("The otherapp payload for this app already exists on SD, that will be used instead of downloading the payload via HTTP.\n");
		#endif
		payload_src = 0;
	}
	else
	{
		memset(filebuffer, 0, filebuffer_maxsize);

		printf("Requesting the actual payload URL with HTTPC...\n");
		ret = http_getactual_payloadurl(payloadurl, payloadurl, sizeof(payloadurl)-2);
		if(ret!=0)
		{
			printf("Failed to request the actual payload URL: 0x%08x.\n", (unsigned int)ret);
			if(ret==0xd8a0a046)
			{
				printf("No usable Internet-connection is likely available.\n");
			}
			else
			{
				printf("If the server isn't down, and the HTTP request was actually done, this may mean your system-version or region isn't supported by the *hax payload currently.\n");
			}
			return ret;
		}

		//Use https instead of http with the below site.
		if(strncmp(payloadurl, "http://smealum.github.io/", 25)==0)
		{
			memmove(&payloadurl[5], &payloadurl[4], strlen(payloadurl)-4);
			payloadurl[4] = 's';
		}

		printf("Downloading the actual payload with HTTPC...\n");
		ret = http_download_payload(payloadurl, &payloadsize);
		if(ret!=0)
		{
			printf("Failed to download the actual payload with HTTP: 0x%08x.\n", (unsigned int)ret);
			if(ret==0xd8a0a046)
			{
				printf("No usable Internet-connection is likely available.\n");
			}
			else
			{
				printf("If the server isn't down, and the HTTP request was actually done, this may mean your system-version or region isn't supported by the *hax payload currently.\n");
			}
			return ret;
		}

		if(ret==0)payload_src = 1;
	}

	#ifdef VERBOSE
	printf("Initializing payload data etc...\n");
	#endif

	payloadsize_aligned = (payloadsize + 0xfff) & ~0xfff;
	if(payloadsize_aligned > PAYLOAD_TEXTMAXSIZE)
	{
		printf("Invalid payload size: 0x%08x.\n", (unsigned int)payloadsize);
		ret = -3;
		return ret;
	}

	if(payload_src)
	{
		hidScanInput();

		if(!(hidKeysHeld() & KEY_Y))
		{
			printf("Saving the downloaded payload to SD since the Y button isn't pressed...\n");
			ret = savesd_payload(payload_sdpath, payloadsize);

			if(ret!=0)
			{
				printf("Payload saving failed: 0x%08x.\n", (unsigned int)ret);
			}
			else
			{
				printf("Payload saving was successful.\n");
			}
		}
		else
		{
			#ifdef VERBOSE
			printf("Skipping saving the downloaded payload to SD since the Y button is pressed.\n");
			#endif
		}
	}

	memcpy(PAYLOAD_TEXTADDR, filebuffer, payloadsize_aligned);
	memset(filebuffer, 0, filebuffer_maxsize);

	ret = svcFlushProcessDataCache(0xffff8001, PAYLOAD_TEXTADDR, payloadsize_aligned);//Flush dcache for the payload which was copied into .text. Since that area was never executed, icache shouldn't be an issue.
	if(ret!=0)
	{
		printf("svcFlushProcessDataCache failed: 0x%08x.\n", (unsigned int)ret);
		return ret;
	}

	paramblk = linearMemAlign(0x10000, 0x1000);
	if(paramblk==NULL)
	{
		ret = 0xfe;
		printf("Failed to alloc the paramblk.\n");
		return ret;
	}

	httpcExit();

	memset(paramblk, 0, 0x10000);

	paramblk[0x1c>>2] = (u32)gxlowcmd_4;
	paramblk[0x20>>2] = (u32)GSPGPU_FlushDataCache;
	paramblk[0x48>>2] = 0x8d;//flags
	paramblk[0x58>>2] = (u32)&gspGpuHandle;

	#ifdef VERBOSE
	printf("Jumping into the payload...\n");
	#endif

	funcptr = (void*)PAYLOAD_TEXTADDR;
	funcptr(paramblk, (u32*)(0x10000000-0x1000));

	ret = 0xff;
	printf("The payload returned back into the app, this should *never* happen with the actual *hax-payload.\n");

	return ret;
}

int main(int argc, char **argv)
{
	Result ret = 0;
	u32 pos;
	Handle kproc_handledup=0;

	// Initialize services
	gfxInitDefault();

	consoleInit(GFX_BOTTOM, NULL);

	printf("hblauncher_loader %s by yellows8.\n", VERSION);

	ret = svcDuplicateHandle(&kproc_handledup, 0xffff8001);
	if(ret!=0)printf("svcDuplicateHandle() with the current proc-handle failed: 0x%08x.\n", (unsigned int)ret);

	if(ret==0)
	{
		for(pos=0; pos<PAYLOAD_TEXTMAXSIZE; pos+=0x1000)
		{
			ret = svcControlProcessMemory(kproc_handledup, (u32)&PAYLOAD_TEXTADDR[pos >> 2], 0x0, 0x1000, MEMOP_PROT, MEMPERM_READ | MEMPERM_WRITE | MEMPERM_EXECUTE);
			if(ret!=0)
			{
				printf("svcControlProcessMemory with pos=0x%x failed: 0x%08x.\n", (unsigned int)pos, (unsigned int)ret);
				break;
			}
		}
	}

	if(ret==0)
	{
		ret = httpcInit(0);
		if(ret!=0)
		{
			printf("Failed to initialize HTTPC: 0x%08x.\n", (unsigned int)ret);
			if(ret==0xd8e06406)
			{
				printf("The HTTPC service is inaccessible.\n");
			}
		}

		if(ret==0)
		{
			filebuffer_maxsize = PAYLOAD_TEXTMAXSIZE;

			filebuffer = (u8*)malloc(filebuffer_maxsize);
			if(filebuffer==NULL)
			{
				printf("Failed to allocate memory.\n");
				ret = -1;
			}
			else
			{
				memset(filebuffer, 0, filebuffer_maxsize);
			}
		}

		if(ret==0)ret = load_hblauncher();

		free(filebuffer);

		httpcExit();
	}

	if(ret!=0 && ret!=0xd8a0a046)printf("An error occured, please report this to here if it persists(or comment on an already existing issue if needed), with an image of your 3DS system: https://github.com/yellows8/hblauncher_loader/issues\n");

	printf("Press the START button to exit.\n");
	// Main loop
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();

		u32 kDown = hidKeysDown();
		if (kDown & KEY_START)
			break;
	}

	// Exit services
	gfxExit();
	return 0;
}


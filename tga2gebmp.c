/**
 * @file tga2gebmp.c
 *
 */
#include <windows.h>
#include <malloc.h>
#include "resource.h"
#include "genesis.h"
#include "ram.h"

#if defined _MSC_VER && _MSC_VER < 1300
    #define GetWindowLongPtr GetWindowLong
    #define GWLP_WNDPROC	GWL_WNDPROC
    #define GWLP_HINSTANCE	GWL_HINSTANCE
    #define GWLP_USERDATA   GWL_USERDATA
#endif


typedef struct	tga2gebmp_WindowData
{
	HINSTANCE	Instance;
	HWND		hwnd;
	HBITMAP		hBitmap;
	geBitmap	*PreviewSkin;
	geVFile		*FSystem;
	char		FileName[_MAX_PATH];
	char		TextureName[_MAX_PATH];
	char		CurrentDirectory[_MAX_PATH];
}	tga2gebmp_WindowData;

static HWND tga2gebmp_DlgHandle = NULL;


void tga2gebmp_InitDialog(HWND hwnd);
void tga2gebmp_UpdatePreview(tga2gebmp_WindowData *pData);

void tga2gebmp_OpenAct(tga2gebmp_WindowData *pData);
void tga2gebmp_OpenTexture(tga2gebmp_WindowData *pData);

void tga2gebmp_ExtractFile(geVFile *VFS, geVFile *File, const char *src, const char *dest);
void tga2gebmp_CopyFile(geVFile *VFS, geVFile *Directory, const char *src, const char *dest);
void tga2gebmp_SaveChanges(tga2gebmp_WindowData *pData);

static	HBITMAP CreateHBitmapFromgeBitmap (geBitmap *Bitmap, HDC hdc);
static	BOOL Render2d_Blit(HDC hDC, HBITMAP Bmp, const RECT *SourceRect, const RECT *DestRect);


static tga2gebmp_WindowData* tga2gebmp_GetWindowData(HWND hwnd)
{
	return ((tga2gebmp_WindowData*)GetWindowLongPtr(hwnd, GWLP_USERDATA));
}


static LRESULT CALLBACK PreviewWndProc
	(
	  HWND hwnd,
	  UINT msg,
	  WPARAM wParam,
	  LPARAM lParam
	)
{
	tga2gebmp_WindowData *pData = tga2gebmp_GetWindowData(hwnd);

	if(msg == WM_PAINT)
	{
		PAINTSTRUCT	ps;
		HDC			hDC;
		RECT		Rect;

		hDC = BeginPaint(hwnd, &ps);
		GetClientRect(hwnd, &Rect);
		Rect.left--;
		Rect.bottom--;
		FillRect(hDC, &Rect, GetStockObject(WHITE_BRUSH));

		if(pData->hBitmap != NULL)
		{
			RECT	Source;
			RECT	Dest;
			HDC		hDC;

			Source.left = 0;
			Source.top = 0;
			Source.bottom = geBitmap_Height(pData->PreviewSkin);
			Source.right = geBitmap_Width(pData->PreviewSkin);
			/* Display bitmaps greater than 1024x1024 resolutions by scaling them into 1024x1024 bitmaps */
			if (Source.bottom > 1024) Source.bottom = 1024;
			if (Source.right  > 1024) Source.right  = 1024;

			Dest = Rect;

			hDC = GetDC(hwnd);
			SetStretchBltMode(hDC, HALFTONE);
			Render2d_Blit(	hDC,
							pData->hBitmap,
							&Source,
							&Dest);
			ReleaseDC(hwnd, hDC);
		}
		EndPaint(hwnd, &ps);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}


static BOOL tga2gebmp_InitWindowData(HWND hwnd)
{
	tga2gebmp_WindowData *pData;

	// allocate window local data structure
	pData = GE_RAM_ALLOCATE_STRUCT(tga2gebmp_WindowData);
	if(pData == NULL)
	{
		DestroyWindow (hwnd);
		return TRUE;
	}

	// and initialize it
	pData->Instance		= (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
	pData->hwnd			= hwnd;
	pData->hBitmap		= NULL;
	pData->PreviewSkin	= NULL;
	pData->FSystem		= NULL;

	// set the window data pointer in the GWLP_USERDATA field
	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pData);
	SetWindowLongPtr(GetDlgItem(hwnd, IDC_PREVIEW), GWLP_USERDATA, (LONG_PTR)pData);
	SetWindowLongPtr(GetDlgItem(hwnd, IDC_PREVIEW), GWLP_WNDPROC, (LONG_PTR)PreviewWndProc);

	// set the program icon on the dialog window
	SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)LoadIcon(pData->Instance, MAKEINTRESOURCE(IDI_ICON1)));
	SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(pData->Instance, MAKEINTRESOURCE(IDI_ICON1)));

	// center dialog on desktop
	{
		RECT WndRect, ScreenRect;
		int Width, Height;

		GetWindowRect(hwnd, &WndRect);
		Width = WndRect.right - WndRect.left;
		Height = WndRect.bottom - WndRect.top;

		GetWindowRect(GetDesktopWindow(), &ScreenRect);
		SetWindowPos(hwnd, 0, (((ScreenRect.right - ScreenRect.left) / 2) - (Width / 2)),
				 (((ScreenRect.bottom - ScreenRect.top) / 2) - (Height / 2)),
				  Width, Height, SWP_NOZORDER | SWP_NOSIZE | SWP_SHOWWINDOW);
	}

	return TRUE;
}


static void tga2gebmp_Shutdown(HWND hwnd, tga2gebmp_WindowData *pData)
{
	if(pData != NULL)
	{
		if(pData->FSystem)
		{
			geVFile_Finder *Finder;
			//geVFile_DeleteFile(pData->FSystem, "$temp$\\Body.bdy");
			geVFile_DeleteFile(pData->FSystem, "$temp$\\Body.tmp");
			Finder = geVFile_CreateFinder(pData->FSystem, "$temp$\\Bitmaps\\*.*");
			if(Finder)
			{
				while(geVFile_FinderGetNextFile(Finder) != GE_FALSE)
				{
					char filename[_MAX_PATH];
					geVFile_Properties Properties;
					geVFile_FinderGetProperties(Finder, &Properties);
					sprintf(filename, "$temp$\\Bitmaps\\%s", Properties.Name);
					geVFile_DeleteFile(pData->FSystem, filename);
				}

				geVFile_DestroyFinder(Finder);
			}

			geVFile_Close(pData->FSystem);

			RemoveDirectory("$temp$\\Bitmaps");
			//RemoveDirectory("$temp$");
		}

		if(pData->PreviewSkin)
			geBitmap_Destroy(&pData->PreviewSkin);

		geRam_Free(pData);
	}

	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
}


static	BOOL Render2d_Blit(HDC hDC, HBITMAP Bmp, const RECT *SourceRect, const RECT *DestRect)
{
	HDC		MemDC;
	int		SourceWidth;
	int		SourceHeight;
	int		DestWidth;
	int		DestHeight;

	MemDC = CreateCompatibleDC(hDC);
	if	(MemDC == NULL)
		return FALSE;

	SelectObject(MemDC, Bmp);

	SourceWidth = SourceRect->right - SourceRect->left;
	SourceHeight = SourceRect->bottom - SourceRect->top;
	DestWidth = DestRect->right - DestRect->left;
	DestHeight = DestRect->bottom - DestRect->top;
	SetStretchBltMode(hDC, COLORONCOLOR);
	StretchBlt(hDC,
					DestRect->left,
					DestRect->top,
					DestHeight,
					DestHeight,
					MemDC,
					SourceRect->left,
					SourceRect->top,
					SourceWidth,
					SourceHeight,
					SRCCOPY);

	DeleteDC(MemDC);

	return TRUE;
}


static BOOL CALLBACK tga2gebmp_DlgProc
	(
	  HWND hwnd,
	  UINT msg,
	  WPARAM wParam,
	  LPARAM lParam
	)
{
	tga2gebmp_WindowData *pData = tga2gebmp_GetWindowData(hwnd);

	switch (msg)
	{
	case WM_INITDIALOG:
		return tga2gebmp_InitWindowData(hwnd);

	case WM_CLOSE :
	case WM_DESTROY :
		tga2gebmp_Shutdown(hwnd, pData);
		PostQuitMessage (0);
		break;

	case WM_COMMAND:
		{
			WORD wNotifyCode = HIWORD (wParam);
			WORD wID = LOWORD (wParam);
			HWND hwndCtl = (HWND)lParam;

			switch (wID)
			{
				case IDC_SKINLIST:
					if(wNotifyCode == LBN_SELCHANGE)
					{
						int Index = SendDlgItemMessage(hwnd, IDC_SKINLIST, LB_GETCURSEL, (WPARAM)0, (LPARAM)0);
						SendDlgItemMessage(hwnd, IDC_SKINLIST, LB_GETTEXT, (WPARAM)Index, (LPARAM)&pData->TextureName[0]);
						if(Index != LB_ERR)
							tga2gebmp_UpdatePreview(pData);
					}
					break;
				case IDC_REPLACE:
					{
						int Index = SendDlgItemMessage(hwnd, IDC_SKINLIST, LB_GETCURSEL, (WPARAM)0, (LPARAM)0);
						if(Index != LB_ERR)
							tga2gebmp_OpenTexture(pData);
					}
					break;
				case IDC_BROWSEACT:
					tga2gebmp_OpenAct(pData);
					break;
				case IDC_SAVE:
					tga2gebmp_SaveChanges(pData);
					break;

			}
		}
		break;

	default :
		break;

	}
	return FALSE;
}


void tga2gebmp_InitDialog(HWND hwnd)
{
	geVFile_Finder *Finder;

	tga2gebmp_WindowData *pData = tga2gebmp_GetWindowData(hwnd);

	GetCurrentDirectory(sizeof(pData->CurrentDirectory), pData->CurrentDirectory);

	pData->FSystem = geVFile_OpenNewSystem(NULL,
									GE_VFILE_TYPE_DOS,
									pData->CurrentDirectory,
									NULL,
									GE_VFILE_OPEN_READONLY | GE_VFILE_OPEN_DIRECTORY);

	geVFile_DeleteFile(pData->FSystem, "$temp$\\Body.bdy");
	geVFile_DeleteFile(pData->FSystem, "$temp$\\Body.tmp");
	Finder = geVFile_CreateFinder(pData->FSystem, "$temp$\\Bitmaps\\*.*");

	while(geVFile_FinderGetNextFile(Finder) != GE_FALSE)
	{
		char filename[_MAX_PATH];
		geVFile_Properties Properties;
		geVFile_FinderGetProperties(Finder, &Properties);
		sprintf(filename, "$temp$\\Bitmaps\\%s", Properties.Name);
		geVFile_DeleteFile(pData->FSystem, filename);
	}

	geVFile_DestroyFinder(Finder);
}


void tga2gebmp_UpdatePreview(tga2gebmp_WindowData *pData)
{
	HWND	PreviewWnd;
	HDC		hDC;
	char	filetys[_MAX_PATH];

	sprintf(filetys, "$temp$\\Bitmaps\\%s", pData->TextureName);

	pData->PreviewSkin = geBitmap_CreateFromFileName(pData->FSystem, filetys);

	PreviewWnd = GetDlgItem(pData->hwnd, IDC_PREVIEW);
	hDC = GetDC(PreviewWnd);

	pData->hBitmap = CreateHBitmapFromgeBitmap(pData->PreviewSkin, hDC);

	ReleaseDC(PreviewWnd, hDC);

	InvalidateRect(GetDlgItem(pData->hwnd, IDC_PREVIEW), NULL, TRUE);
}


void tga2gebmp_OpenTexture(tga2gebmp_WindowData *pData)
{
	OPENFILENAME ofn;
	char Filter[_MAX_PATH];
	char	Dir[_MAX_PATH];
	geVFile *file;
	geBitmap *bitmap;
	char WriteFileName[256];
	char OpenFileName[_MAX_PATH];

	OpenFileName[0] = '\0';

	GetCurrentDirectory(sizeof(Dir), Dir);

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = pData->hwnd;
	ofn.hInstance = pData->Instance;

	{
		char *c;

		strcpy(Filter, "*.tga;*.bmp");
		c = &Filter[strlen(Filter)] + 1;
		strcpy (c, "*.*");
		c = &c[strlen(c)] + 1;
		*c = '\0';
	}
	ofn.lpstrFilter = Filter;
	ofn.lpstrCustomFilter = NULL;
	ofn.nMaxCustFilter = 0;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = OpenFileName;
	ofn.nMaxFile = sizeof(OpenFileName);
	ofn.lpstrFileTitle = OpenFileName;
	ofn.nMaxFileTitle = sizeof(OpenFileName);
	ofn.lpstrInitialDir = Dir;
	ofn.lpstrTitle = NULL;
	ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
	ofn.nFileOffset = 0;
	ofn.nFileExtension = 0;
	ofn.lpstrDefExt = "tga";
	ofn.lCustData = 0;
	ofn.lpfnHook = NULL;
	ofn.lpTemplateName = NULL;

	if(!GetOpenFileName (&ofn))
		return;

	bitmap = geBitmap_CreateFromFileName(NULL, OpenFileName);

	if(bitmap)
	{
		sprintf(WriteFileName, "$temp$\\Bitmaps\\%s", pData->TextureName);
		file = geVFile_Open(pData->FSystem, WriteFileName, GE_VFILE_OPEN_CREATE);
		if(file)
		{
			geBitmap_WriteToFile(bitmap, file);
			geVFile_Close(file);
		}
		geBitmap_Destroy(&bitmap);
	}
}


void tga2gebmp_CopyFile(geVFile *srcVFS, geVFile *destVFS, const char *src, const char *dest)
{
	geVFile *SrcFile = NULL;
	geVFile *DestFile = NULL;

	SrcFile = geVFile_Open(srcVFS, src, GE_VFILE_OPEN_READONLY);
	if(!SrcFile)
	{
		return;
	}

	DestFile = geVFile_Open(destVFS, dest, GE_VFILE_OPEN_CREATE);
	if(!DestFile)
	{
		geVFile_Close(SrcFile);
		return;
	}

	{
		char CopyBuf[16384];
		int CopyBufLen = 16384;
		long Size;
		geVFile_Properties Props;

		if(!geVFile_GetProperties(SrcFile, &Props))
			return;

		if(!geVFile_Size(SrcFile, &Size))
			return;

		while(Size)
		{
			int CurLen = min(Size, CopyBufLen);

			if(!geVFile_Read(SrcFile, CopyBuf, CurLen))
				return;

			if(!geVFile_Write(DestFile, CopyBuf, CurLen))
				return;

			Size -= CurLen;
		}
	}

	geVFile_Close(DestFile);
	geVFile_Close(SrcFile);
}


void tga2gebmp_SaveChanges(tga2gebmp_WindowData *pData)
{
	char working[256];
	geVFile_Finder	*Finder;
	geVFile			*destVFS;
	geVFile			*srcVFS;
	geVFile			*Directory;

	// this will become the new body file with updated textures
	sprintf(working, "%s\\$temp$\\Body.tmp", pData->CurrentDirectory);
	destVFS = geVFile_OpenNewSystem(NULL, GE_VFILE_TYPE_VIRTUAL, working, NULL, GE_VFILE_OPEN_CREATE | GE_VFILE_OPEN_DIRECTORY);
	if(!destVFS)
	{
		return;
	}

	// the old body we extracted from the .act file
	sprintf(working, "%s\\$temp$\\Body.bdy", pData->CurrentDirectory);
	srcVFS = geVFile_OpenNewSystem(NULL, GE_VFILE_TYPE_VIRTUAL, working, NULL, GE_VFILE_OPEN_READONLY | GE_VFILE_OPEN_DIRECTORY);
	if(!srcVFS)
	{
		geVFile_Close(destVFS);
		return;
	}

	// create bitmap directory in new body file
	Directory = geVFile_Open(destVFS, "Bitmaps", GE_VFILE_OPEN_DIRECTORY|GE_VFILE_OPEN_CREATE);
	if(!Directory)
	{
		geVFile_Close(destVFS);
		geVFile_Close(srcVFS);
		return;
	}
	geVFile_Close(Directory);

	// copy the geBitmap filess from the temp folder to the new bdy file
	Finder = geVFile_CreateFinder(pData->FSystem, "$temp$\\Bitmaps\\*.*");
	if(!Finder)
	{
		geVFile_Close(destVFS);
		geVFile_Close(srcVFS);
		return;
	}

	while(geVFile_FinderGetNextFile(Finder) != GE_FALSE)
	{
		char filename[_MAX_PATH];
		char filename2[_MAX_PATH];

		geVFile_Properties	Properties;
		geVFile_FinderGetProperties(Finder, &Properties);

		sprintf(filename, "$temp$\\Bitmaps\\%s", Properties.Name);
		sprintf(filename2, "Bitmaps\\%s", Properties.Name);
		tga2gebmp_CopyFile(pData->FSystem, destVFS, filename, filename2);
	}

	geVFile_DestroyFinder(Finder);

	// copy over the geometry
	tga2gebmp_CopyFile(srcVFS, destVFS, "Geometry", "Geometry");

	geVFile_Close(destVFS);
	geVFile_Close(srcVFS);

	// next copy the new body to the new actor file (FileName), rename the original one
	{
		char oldFileName[_MAX_PATH];
		sprintf(oldFileName, "%s.old", pData->FileName);
		rename(pData->FileName, oldFileName);
		srcVFS = geVFile_OpenNewSystem(NULL, GE_VFILE_TYPE_VIRTUAL, oldFileName, NULL, GE_VFILE_OPEN_READONLY | GE_VFILE_OPEN_DIRECTORY);
		destVFS = geVFile_OpenNewSystem(NULL, GE_VFILE_TYPE_VIRTUAL, pData->FileName, NULL, GE_VFILE_OPEN_CREATE | GE_VFILE_OPEN_DIRECTORY);

		if(Directory = geVFile_Open(destVFS, "Motions", GE_VFILE_OPEN_DIRECTORY|GE_VFILE_OPEN_CREATE))
			geVFile_Close(Directory);

		Finder = geVFile_CreateFinder(srcVFS, "Motions\\*.*");

		while(geVFile_FinderGetNextFile(Finder) != GE_FALSE)
		{
			char filename[256];
			char filename2[256];
			geVFile_Properties	Properties;
			geVFile_FinderGetProperties(Finder, &Properties);
			sprintf(filename, "Motions\\%s", Properties.Name);
			sprintf(filename2, "Motions\\%s", Properties.Name);
			tga2gebmp_CopyFile(srcVFS, destVFS, filename, filename2);
		}
		geVFile_DestroyFinder(Finder);
		tga2gebmp_CopyFile(srcVFS, destVFS, "Header", "Header");
		tga2gebmp_CopyFile(pData->FSystem, destVFS, "$temp$\\Body.tmp", "Body");

		geVFile_Close(srcVFS);
		geVFile_Close(destVFS);
	}
}


void tga2gebmp_OpenAct(tga2gebmp_WindowData *pData)
{
	OPENFILENAME ofn;
	geVFile_Finder *Finder;
	geVFile		*Directory;
	geVFile		*VFS;
	char		Filter[_MAX_PATH];
	char		Dir[_MAX_PATH];
	char		TempName[_MAX_PATH];

	pData->FileName[0] = '\0';

	GetCurrentDirectory(sizeof(Dir), Dir);

	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = pData->hwnd;
	ofn.hInstance = pData->Instance;
	{
		char *c;

		strcpy (Filter, "Genesis3d Actors (*.act)");
		c = &Filter[strlen (Filter)] + 1;
		strcpy (c, "*.act");
		c = &c[strlen (c)] + 1;
		*c = '\0';
	}
	ofn.lpstrFilter = Filter;
	ofn.lpstrCustomFilter = NULL;
	ofn.nMaxCustFilter = 0;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = pData->FileName;
	ofn.nMaxFile = sizeof(pData->FileName);
	ofn.lpstrFileTitle = pData->FileName;
	ofn.nMaxFileTitle = sizeof(pData->FileName);
	ofn.lpstrInitialDir = Dir;
	ofn.lpstrTitle = NULL;
	ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
	ofn.nFileOffset = 0;
	ofn.nFileExtension = 0;
	ofn.lpstrDefExt = "act";
	ofn.lCustData = 0;
	ofn.lpfnHook = NULL;
	ofn.lpTemplateName = NULL;

	if(!GetOpenFileName (&ofn))
		return;

	{
		if(pData->FSystem)
		{
			geVFile_Finder *Finder;
			geVFile_DeleteFile(pData->FSystem, "$temp$\\Body.bdy");
			geVFile_DeleteFile(pData->FSystem, "$temp$\\Body.tmp");
			Finder = geVFile_CreateFinder(pData->FSystem, "$temp$\\Bitmaps\\*.*");
			if(Finder)
			{
				while(geVFile_FinderGetNextFile(Finder) != GE_FALSE)
				{
					char filename[_MAX_PATH];
					geVFile_Properties Properties;
					geVFile_FinderGetProperties(Finder, &Properties);
					sprintf(filename, "$temp$\\Bitmaps\\%s", Properties.Name);
					geVFile_DeleteFile(pData->FSystem, filename);
				}

				geVFile_DestroyFinder(Finder);
			}

			RemoveDirectory("$temp$\\Bitmaps");
			RemoveDirectory("$temp$");
		}

		SendDlgItemMessage(pData->hwnd, IDC_SKINLIST, LB_RESETCONTENT, (WPARAM)0, (LPARAM)0);
	}

	Directory = geVFile_Open(pData->FSystem, "$temp$", GE_VFILE_OPEN_DIRECTORY | GE_VFILE_OPEN_CREATE);
	if(Directory)
		geVFile_Close(Directory);

	VFS = geVFile_OpenNewSystem(NULL, GE_VFILE_TYPE_VIRTUAL, pData->FileName, NULL, GE_VFILE_OPEN_READONLY | GE_VFILE_OPEN_DIRECTORY);

	// extract the body file to the temp folder
	tga2gebmp_ExtractFile(VFS, pData->FSystem, "Body", "Body.bdy");

	// create bitmaps folder
	Directory = geVFile_Open(pData->FSystem, "$temp$\\Bitmaps", GE_VFILE_OPEN_DIRECTORY | GE_VFILE_OPEN_CREATE);
	if(Directory)
		geVFile_Close(Directory);

	geVFile_Close(VFS);

	// extract geBitmap files from already extracted body file
	sprintf(TempName, "%s\\$temp$\\Body.bdy", pData->CurrentDirectory);
	VFS = geVFile_OpenNewSystem(NULL, GE_VFILE_TYPE_VIRTUAL, TempName, NULL, GE_VFILE_OPEN_READONLY | GE_VFILE_OPEN_DIRECTORY);

	Finder = geVFile_CreateFinder(VFS, "Bitmaps\\*.*");
	while(geVFile_FinderGetNextFile(Finder) != GE_FALSE)
	{
		char filename[256];
		geVFile_Properties	Properties;
		geVFile_FinderGetProperties(Finder, &Properties);
		sprintf(filename, "Bitmaps\\%s", Properties.Name);
		tga2gebmp_ExtractFile(VFS, pData->FSystem, filename, filename);
		SendDlgItemMessage(pData->hwnd, IDC_SKINLIST, LB_ADDSTRING, (WPARAM)0, (LPARAM)Properties.Name);
	}

	geVFile_DestroyFinder(Finder);

	geVFile_Close(VFS);

	SendDlgItemMessage(pData->hwnd, IDC_SKINLIST, LB_SETCURSEL, 0, 0);

	{
		LRESULT Index = SendDlgItemMessage(pData->hwnd, IDC_SKINLIST, LB_GETCURSEL, (WPARAM)0, (LPARAM)0);
		SendDlgItemMessage(pData->hwnd, IDC_SKINLIST, LB_GETTEXT, (WPARAM)Index, (LPARAM)&pData->TextureName[0]);
		if(Index != LB_ERR)
			tga2gebmp_UpdatePreview(pData);
	}
}


void tga2gebmp_ExtractFile(geVFile *srcVFS, geVFile *destVFS, const char *src, const char *dest)
{
	geVFile *	SrcFile;
	geVFile *   DestFile;
	char destTemp[_MAX_PATH];
	sprintf(destTemp, "$temp$\\%s", dest);

	SrcFile = geVFile_Open(srcVFS, src, GE_VFILE_OPEN_READONLY);
	if(!SrcFile)
	{
		return;
	}
	DestFile = geVFile_Open(destVFS, destTemp, GE_VFILE_OPEN_CREATE);
	if(!DestFile)
	{
		geVFile_Close(SrcFile);
		return;
	}

	do
	{
		char CopyBuf[16384];
		int CopyBufLen = 16384;
		long Size;
		geVFile_Properties Props;

		if(!geVFile_GetProperties(SrcFile, &Props))
			break;

		if(!geVFile_Size(SrcFile, &Size))
			break;

		while(Size)
		{
			int CurLen = min(Size, CopyBufLen);

			if(!geVFile_Read(SrcFile, CopyBuf, CurLen))
				break;

			if(!geVFile_Write(DestFile, CopyBuf, CurLen))
				break;

			Size -= CurLen;
		}
	}while(GE_FALSE);

	geVFile_Close(DestFile);
	geVFile_Close(SrcFile);
}


int CALLBACK WinMain
	(
		HINSTANCE instance,
		HINSTANCE prev_instance,
		LPSTR cmd_line,
		int cmd_show
	)
{
	MSG Msg;

	tga2gebmp_DlgHandle = CreateDialog
	(
		instance,
		MAKEINTRESOURCE (IDD_MAINWINDOW),
		NULL,
		tga2gebmp_DlgProc
	);

	if (tga2gebmp_DlgHandle == NULL)
	{
		return 0;
	}

	tga2gebmp_InitDialog(tga2gebmp_DlgHandle);

	ShowWindow (tga2gebmp_DlgHandle, SW_SHOWNORMAL);
	UpdateWindow (tga2gebmp_DlgHandle);


	while(GetMessage(&Msg, NULL, 0, 0))
	{
		UpdateWindow(tga2gebmp_DlgHandle);

		if (!IsDialogMessage (tga2gebmp_DlgHandle, &Msg))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
	}

	return 0;
}


static HBITMAP CreateHBitmapFromgeBitmap (geBitmap *Bitmap, HDC hdc)
{
	geBitmap *Lock;
	gePixelFormat Format;
	geBitmap_Info info;
	HBITMAP hbm = NULL;

	Format = GE_PIXELFORMAT_24BIT_BGR;

	if(geBitmap_GetBits(Bitmap))
	{
		Lock = Bitmap;
	}
	else
	{

		if(!geBitmap_LockForRead(Bitmap, &Lock, 0, 0, Format, GE_FALSE, 0))
		{
			return NULL;
		}
	}

	geBitmap_GetInfo(Lock, &info, NULL);

	if(info.Format != Format)
		return NULL;

	{
		void *bits;
		BITMAPINFOHEADER bmih;
		int pelbytes;

		pelbytes = gePixelFormat_BytesPerPel(Format);
		bits = geBitmap_GetBits(Lock);

		bmih.biSize = sizeof(bmih);
		bmih.biHeight = - info.Height;
		bmih.biPlanes = 1;
		bmih.biBitCount = 24;
		bmih.biCompression = BI_RGB;
		bmih.biSizeImage = 0;
		bmih.biXPelsPerMeter = bmih.biYPelsPerMeter = 10000;
		bmih.biClrUsed = bmih.biClrImportant = 0;

		/* Display bitmaps greater than 1024x1024 resolutions by scaling them into 1024x1024 bitmaps */
		if ( abs(info.Height) > 1024 ) //display large bitmaps (> 2048 x 2048)
		{
			void * newbits;
			int Stride;

			bmih.biWidth = 1024;
			bmih.biHeight = - 1024;

			Stride = (((1024 * pelbytes)+3)&(~3));
			newbits = geRam_Allocate(Stride * 1024);

			if(newbits)
			{
				char *newptr, *oldptr;
				int y;
				int z;
				int c = info.Width / 1024;

				newptr = (char*)newbits;
				oldptr = (char*)bits;

				for(y=0; y<1024; y++)
				{
					newptr = (char*) newbits + (y * Stride);
					oldptr = (char*) bits    + (c * (y * (info.Stride*pelbytes)));

					for(z=0; z<1024; z++)
					{
						memcpy ( newptr, oldptr, pelbytes );
						oldptr += ( c * pelbytes );
						newptr += pelbytes;
					}

				}

				hbm = CreateDIBitmap( hdc, &bmih , CBM_INIT , newbits, (BITMAPINFO *)&bmih , DIB_RGB_COLORS );
				geRam_Free(newbits);
			}

		}
		else
		{
			if((info.Stride*pelbytes) == (((info.Stride*pelbytes)+3)&(~3)))
			{
				bmih.biWidth = info.Stride;
				hbm = CreateDIBitmap(hdc, &bmih , CBM_INIT , bits, (BITMAPINFO*)&bmih , DIB_RGB_COLORS);
			}
			else
			{
				void *newbits;
				int Stride;

				bmih.biWidth = info.Width;
				Stride = (((info.Width*pelbytes)+3)&(~3));
				newbits = geRam_Allocate(Stride * info.Height);
				if(newbits)
				{
					char *newptr, *oldptr;
					int y;

					newptr = (char*)newbits;
					oldptr = (char*)bits;
					for(y=0; y<info.Height; y++)
					{
						memcpy(newptr, oldptr, (info.Width)*pelbytes);
						oldptr += info.Stride*pelbytes;
						newptr += Stride;
					}
					hbm = CreateDIBitmap(hdc, &bmih , CBM_INIT , newbits, (BITMAPINFO*)&bmih , DIB_RGB_COLORS);
					geRam_Free(newbits);
				}
			}
		}
	}

	if(Lock != Bitmap)
	{
		geBitmap_UnLock(Lock);
	}

	return hbm;
}


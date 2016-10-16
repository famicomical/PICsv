/*
	PICsv		(RGB2PIC改名)
				BMP,RGB,Q0 -> PIC(TYPE 15 24ﾋﾞｯﾄ色) ｺﾝﾊﾞｰﾀ
	
		コンパイルは far dataモデル(ｺﾝﾊﾟｸﾄ､ﾗｰｼﾞ､ﾋｭｰｼﾞ)で行なうこと

*/

#ifdef __TURBOC__	/* TC(++),BC++用かな... たぶん QC でも大丈夫かも...?    */
	/*MSDOSな環境なら*/
	#define FAR_ALLOC	/* callocの代わりにfarcalloc を使う場合, 定義		*/
	#define DIRENTRY/* ﾜｲﾙﾄﾞ･ｶｰﾄﾞ対応のﾌｧｲﾙ名取得ﾙｰﾁﾝを使う.				*/
	#define FDATEGET/* ファイルの日付を保存するオプションを付加				*/
#else
	#define huge
	#define far
#endif

/*---------------------------------------------------------------------------*/
/*					共　通　												 */
/*---------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef FAR_ALLOC
#include <alloc.h>
#endif
#if 0
	#define KEYBRK	/* CTRL-C で abort 可にする...? */
	#include <signal.h>
#endif


typedef short SHORT;
typedef unsigned short WORD;
typedef unsigned int  DWORD;
typedef unsigned int  uint;

#ifndef toascii /* ctype.h がincludeされていないとき */
  #define toupper(c)  ( ((c) >= 'a' && (c) <= 'z') ? (c) - 0x20 : (c) )
  #define isdigit(c)  ((c) >= '0' && (c) <= '9')
#endif

#define FNAMESIZE	128

/*---------------------------------------------------------------------------*/
typedef DWORD PIXEL;		/* 1ピクセルを収める型 */
/*
#define GetPixR(col)	((char)((WORD)(col) >> 8) & 0xff))
#define GetPixG(col)	((char)((DWORD)(col) >> 16) & 0xff))
#define GetPixB(col)	((char)((col) & 0xff))
*/

/*- 日付保持のために使用 ----------------------------------------------------*/
#ifdef FDATEGET
#ifdef TCC/*__TURBOC__*/	/* TC用 ... BCや MSなC では不要 */
	#include <dos.h>

unsigned _dos_setftime(int hno, unsigned dat, unsigned tim)
{
	union REGS reg;

	reg.x.ax = 0x5701;
	reg.x.bx = hno;
	reg.x.cx = tim;
	reg.x.dx = dat;
	intdos(&reg, &reg);
	if (reg.x.flags & 1) {/* reg.x.flags はTCに依存 */
		return (unsigned)(errno = reg.x.ax);
	}
	return 0;
}

unsigned _dos_getftime(int hno, unsigned *dat, unsigned *tim)
{
	union REGS reg;

	reg.x.ax = 0x5700;
	reg.x.bx = hno;
	intdos(&reg, &reg);
	if (reg.x.flags & 1) {	/* reg.x.flags はTCに依存 */
		return (unsigned)(errno = reg.x.ax);
	}
	*tim = reg.x.cx;
	*dat = reg.x.dx;
	return 0;
}
#endif
#endif

/*---------------------------------------------------------------------------*/
#if 0
unsigned char *FIL_BaseName(unsigned char *adr)
{
	unsigned char *p;

	p = adr;
	while (*p != '\0') {
		if (*p == ':' || *p == '/' || *p == '\\') {
			adr = p + 1;
		}
		if (iskanji(*p) && *(p+1) ) {
			p++;
		}
		p++;
	}
	return adr;
}
#endif

char *FIL_ChgExt(char filename[], char *ext)
{
	char *p;

	if (filename[0] == '.') {
		filename ++;
	}
	if (filename[0] == '.') {
		filename ++;
	}
	p = strrchr(filename, '/');
	if ( p == NULL) {
		p = filename;
	}
	p = strrchr(p, '\\');
	if ( p == NULL) {
		p = filename;
	}
	p = strrchr( p, '.');
	if (p == NULL) {
		strcat(filename,".");
		strcat( filename, ext);
	} else {
		strcpy(p+1, ext);
	}
	return filename;
}

char *FIL_AddExt(char filename[], char *ext)
{
	char *p;

	if (filename[0] == '.') {
		filename ++;
	}
	if (filename[0] == '.') {
		filename ++;
	}
	p = strrchr(filename, '/');
	if ( p == NULL) {
		p = filename;
	}
	p = strrchr(p, '\\');
	if ( p == NULL) {
		p = filename;
	}
	if ( strrchr( p, '.') == NULL) {
		strcat(filename,".");
		strcat(filename, ext);
	}
	return filename;
}


void *calloc_m(uint nobj, uint sz)
{
	void *p;
  #ifdef FAR_ALLOC
	p = farcalloc(nobj,sz);
  #else
	p = calloc(nobj,sz);
  #endif
	if(p == NULL) {
		printf("required memory (%d*%d) cannot be secured\n",nobj,sz);
	}
	return p;
}

FILE *fopen_m(char *name, char *mode)
{
	FILE *fp;

	fp = fopen(name,mode);
	if (fp == NULL) {
		printf("%s cannot be opened\n",name);
	}
	return fp;
}

/*---------------------------------------------------------------------------*/
/*							P	I	C	出	力								 */
/*---------------------------------------------------------------------------*/
/*module PIC*/

#define PIC_WRTBUFSIZ	0x4000
#define PIC_COMMENTSIZ	0x1000		/* PICコメント・バッファ・サイズ */

/* PIC_PutLinesにわたす１行出力を行う関数(へのポインタ)の型 */
typedef void (*PIC_FUNC_GETLINE)(void *C, char *buf);

int PIC_wrtDebFlg = 0;	/* !0 ときデバッグ	 1=counter	2=msg*/

/* PIC-TYPE */
#define PIC_X68K	0
#define PIC_88VA	1
#define PIC_TOWNS	2
#define PIC_MAC 	3
#define PIC_EX		15


/*-- 128個分の色キャッシュ関係 --*/
typedef struct PIC_C7TBL {
	PIXEL col;
	int  lft;
	int  rig;
} PIC_C7TBL;

typedef struct PIC {
	FILE *fp;			/* ファイル・ポインタ								*/
	char *name; 		/* ファイル名										*/
	int  xsize,ysize;	/* サイズ　横幅,縦幅								*/
	int  xstart,ystart; /* 基点座標											*/
	int  colbit;		/* 色ビット数										*/
	int  asp1, asp2;	/* アスペクト比. 横,縦								*/
	int  macNo; 		/* 機種番号: 0=X68K 1=88VA 2=TOWNS 3=MAC 15=拡張ﾍｯﾀﾞ*/
						/*			 (廃案 内部処理用:$101=MSX(/MMﾍｯﾀﾞ))	*/
	int  macMode;		/* 機種依存モード									*/
	WORD typ;			/* macMode*0x10 + macNo								*/
	char *comment;		/* コメント											*/
	int  palbit;		/* パレット１つのビット数							*/
	char *rgb;			/* rgbパレット										*/

/* private: */
	/* 入力関係 */
	int  sizeX8;		/* 8ﾄﾞｯﾄ単位での横幅. cmap用						*/
	int  wrtdatlen;		/* ﾋﾞｯﾄ出力の残りﾋﾞｯﾄ数								*/
	char wrtdat;		/* ビット出力での１バイト							*/
	uint wrtpos;		/* 出力バッファでの現在の位置						*/
	uint wrtbufsiz;	/* 出力バッファのサイズ								*/
	char *wrtbuf;		/* 出力バッファへのポインタ							*/

	/* 変換関係 */
	char huge *cmap;	/* 連鎖を記録するためのバッファ						*/
	char **plin;	 	/* ピクセル・データ									*/
	char *tmplin;	 	/* ピクセル・データ 24bitCol->16bitCol				*/
	int plinCnt;		/* plin の行数										*/
	int c7p;			/* 色キャッシュの最新の位置							*/
	PIC_C7TBL *c7t; 	/* 色キャッシュ・テーブル							*/
	void  (*wrtCol)(struct PIC *,PIXEL col);/* 色outputﾙｰﾁﾝへのポインタ		*/
	PIXEL (*getPix)(struct PIC *, int x, int y);/* 色inputﾙｰﾁﾝへのポインタ	*/

} PIC;


void PIC_InitWrtBuf(PIC *pic,uint wrtBufSz)
{
	pic->wrtdat = 0;
	pic->wrtdatlen = 8;
	pic->wrtpos = 0;
	pic->wrtbufsiz = wrtBufSz;
	pic->wrtbuf = (char*) calloc_m(1, wrtBufSz);
	if (pic->wrtbuf == NULL) {
		exit(1);
	}
}

static int	PIC_WriteBuf(PIC *pic, uint siz)
	/* バッファに読込む */
{
	uint n;

	if (siz) {
		n = fwrite(pic->wrtbuf, 1, siz, pic->fp);
		if (PIC_wrtDebFlg == 2) {
			printf("write %u->%u\n",siz,n);
		}
		if (n != siz) {
			printf("PIC output write error\n");
			exit(1);
		}
	}
	pic->wrtpos = 0;
	return 0;
}

static void  PIC_WrtByte(PIC *pic,int c)
{
	pic->wrtbuf[pic->wrtpos++] = (char)c;
	if (pic->wrtpos >= pic->wrtbufsiz) {
		PIC_WriteBuf(pic, pic->wrtbufsiz);
	}
}



static void PIC_WrtWord(PIC *pic,WORD c)
{
	PIC_WrtByte(pic, ((c >> 8)&0xff));
	PIC_WrtByte(pic, (c & 0xff));
}

static void  PIC_FlushBuf(PIC *pic)
{
  #if 1	
  	/* ほんとはPIC.wrtdatを2バイトにして16ビット長にして処理するのが */
  	/* 正しいのだがとりあえず、余分に１バイト出力することでバグ回避  */
	PIC_WrtByte(pic,0);
  #endif
	if (pic->wrtpos) {
		PIC_WriteBuf(pic, pic->wrtpos);
	}
}


void PIC_WrtBit(PIC *pic, int flg)
	/* 1 ビット書出 */
{
	pic->wrtdat = pic->wrtdat + pic->wrtdat + (flg != 0);
	if (--pic->wrtdatlen == 0) {
		PIC_WrtByte(pic,pic->wrtdat);
		pic->wrtdat = 0;
		pic->wrtdatlen = 8;
	}
}


void PIC_WrtBits(PIC *pic, int sz, int dat)
	/* sz ビット dat の下位から書き出します */
{
  #if 1
	int i,n;

	dat <<= 32 - sz;
	n = pic->wrtdat;
	while ( sz >= pic->wrtdatlen) {
		for ( i = 0; i < pic->wrtdatlen; i++) {
			n = n + n + (dat < 0);	/* n = (n << 1) + (dat&0x7fffffffL)?1:0);*/
			dat <<= 1;
		}
		PIC_WrtByte(pic,n);
		n = 0;
		sz -= pic->wrtdatlen;
		pic->wrtdatlen = 8;
	}
	for ( i = 0; i < sz; i++) {
		n = n + n + (dat < 0);
		dat <<= 1;
	}
	pic->wrtdatlen -= sz;
	pic->wrtdat = n;
 #else
	dat <<= 32 - sz;
 	while(sz-- > 0) {
 		PIC_WrtBit(pic,(dat < 0));
 		dat <<= 1;
 	}
 #endif
}

static void PIC_WrtLen(PIC *pic, int len)
	/* 長さを書き出します */
{
	int 	a;
	int	b;

	a = 1;
	b = 2;
	while (len > b) {
		++a;
		b = (b+1) * 2;
	}
	PIC_WrtBits(pic, a, 0xfffffffeL);
	PIC_WrtBits(pic, a, len - (b>>1));
}

static void PIC_Free(PIC *pic)
	/* PIC読込みで確保したメモリを解放*/
{
	if (pic->fp) {
		fclose(pic->fp);
	}
	// if (pic->name) {
	// 	free(pic->name);
	// }
	if (pic->wrtbuf) {
		free(pic->wrtbuf);
	}
	if (pic->comment) {
		free(pic->comment);
	}
	if (pic->c7t) {
		free(pic->c7t);
	}
	if (pic->tmplin) {
		free(pic->tmplin);
	}
	free(pic);
}

void PIC_CloseW(PIC *pic)
{
	PIC_Free(pic);
}

static void PIC_PutMM(PIC *pic,int x68kflg, char *artist)
{
	int l;
	int i,n,asp68,x0,y0;
	char *p;
	char name[20];

	memset(name,0,20);
	if (artist) {
		strncpy(name, artist, 18);
		name[18] = 0;
	}
	artist = name;

	asp68 = 0;
	x0 = y0 = -1;
	if (x68kflg) {					/* apic のときのアスペクト比の扱い */
		l = (int)pic->asp1 * 100L / pic->asp2;
		if (l < 75) {
			asp68 = 1;
		} else if (l < 120) {
			asp68 = (pic->colbit == 4) ? 0 : 2;
		} else {
			asp68 = (pic->colbit != 4) ? 0 : 3;
		}
		x0 = pic->xstart;
		y0 = pic->ystart;
	  #if 1
		if (x0 <= 0)
			x0 = -1;
		if (y0 <= 0)
			y0 = -1;
		if (x0 < 0 || y0 < 0)
			x0 = y0 = -1;
	  #endif
	}

	if (artist && *artist) {
		/* MMヘッダ中には空白を置けないことになっているので、作者名の前後の	*/
		/* 空白を取り除き、名前中の空白は'_'に置き換えることにする			*/

		/* 行末空白を取る */
		n = strlen(artist);
		for (i = n-1; i >= 0; i--) {
			if (artist[i] <= 0x20 || artist[i] == 0x7f)
				artist[i] = 0;
			else
				break;
		}
		/* 行頭空白を取る */
		n = strlen(artist);
		for (p = artist, i = 0; i < n; i++) {
			if (artist[i] <= 0x20 || artist[i] == 0x7f)
				p++;
			else
				break;
		}
		artist = p;
		/* 行中空白を'_'に置き換える */
		n = strlen(artist);
		for (i = 0; i < n; i++) {
			if (artist[i] < 0x20 || artist[i] == 0x7f)
				artist[i] = '_';
		}
	}

	/* MMヘッダ生成不要なら帰る */
	if ((x0 < 0 && y0 < 0) && asp68 == 0 && (artist == NULL || *artist=='\0'))
		return;

	/* MMヘッダ開始 */
	PIC_WrtByte(pic,'/');
	PIC_WrtByte(pic,'M');
	PIC_WrtByte(pic,'M');

	/* 始点生成 */
	if (x0 >= 0 && y0 >= 0) {
		PIC_WrtByte(pic,'/'), PIC_WrtByte(pic, 'X'), PIC_WrtByte(pic,'Y');

		PIC_WrtByte(pic, '0'+(pic->xstart / 1000)%10);
		PIC_WrtByte(pic, '0'+(pic->xstart /  100)%10);
		PIC_WrtByte(pic, '0'+(pic->xstart /   10)%10);
		PIC_WrtByte(pic, '0'+(pic->xstart       )%10);

		PIC_WrtByte(pic, '0'+(pic->ystart / 1000)%10);
		PIC_WrtByte(pic, '0'+(pic->ystart /  100)%10);
		PIC_WrtByte(pic, '0'+(pic->ystart /   10)%10);
		PIC_WrtByte(pic, '0'+(pic->ystart       )%10);
	}

	/* 比率情報生成 */
	if (asp68) {
		PIC_WrtByte(pic,'/');
		if (asp68 == 1) {
			PIC_WrtByte(pic, 'M'), PIC_WrtByte(pic,'Y');
		} else if (asp68 == 2) {
			PIC_WrtByte(pic, 'X'), PIC_WrtByte(pic,'S'), PIC_WrtByte(pic,'S');
		} else /*if (asp68 == 3)*/ {
			PIC_WrtByte(pic, 'X'), PIC_WrtByte(pic,'S'), PIC_WrtByte(pic,'N');
		}
	}
	/* 作者名生成 */
	if (artist && *artist) {
		PIC_WrtByte(pic,'/'), PIC_WrtByte(pic, 'A'), PIC_WrtByte(pic,'U');
		for (i = 0; i < 18 && artist[i]; i++) {
			PIC_WrtByte(pic, artist[i]);
		}
	}
	
	/* MM ヘッダ終了 */
	PIC_WrtByte(pic,'/');
	PIC_WrtByte(pic,':');
}


PIC *PIC_Create(char *name, int colbit, int xsize, int ysize,
				int xstart, int ystart, int asp1, int asp2, int palbit,
				char *rgb, int x68kflg, char *comment, char *artist)
{
	PIC *pic;
	int l;

	if (PIC_wrtDebFlg == 0 && x68kflg == 0 && (colbit == 4||colbit == 12)) {
		printf("4096 color image cannot be converted to PIC\n");
		return NULL;
	}
	pic = (PIC*) calloc_m(1, sizeof(PIC));
	if (pic == NULL) {
		return NULL;
	}
	pic->colbit = colbit;
	pic->palbit = palbit;
	pic->xsize = xsize;
	pic->ysize = ysize;
	pic->xstart = xstart;
	pic->ystart = ystart;
	if (asp1 == 0 || asp2 == 0) {
		asp1 = asp2 = 1;
	}
	pic->asp1 = asp1;
	pic->asp2 = asp2;
	pic->rgb  = rgb;
	pic->name = name;

	l = (int)asp1 * 100L / asp2;
	if (asp1 == asp2 || (l >= 90 && l <= 110)) {
		pic->typ = 0x0f;
	} else if (l >= 125 && l <= 180) {
		pic->typ = 0x1f;
	} else {
		pic->typ = 0xff;
	}

	if (x68kflg) {	/* apic にするとき */
		if (pic->colbit > 16) {
			pic->colbit = 16;
		} else if (pic->colbit >= 9 && pic->colbit < 15) {
			pic->colbit = 15;
		}
		pic->typ = 0;
		if (pic->ysize > 512) {
			printf("images with more than 512 lines cannot be converted to (A)PIC TYPE 0\n");
			return NULL;
		}
	}

	pic->fp = fopen_m(name,"wb");
	if (pic->fp == NULL) {
		return NULL;
	}
	PIC_InitWrtBuf(pic,PIC_WRTBUFSIZ);
	PIC_WrtByte(pic,'P');
	PIC_WrtByte(pic,'I');
	PIC_WrtByte(pic,'C');
	PIC_PutMM(pic, x68kflg, artist);
	if (comment) {
		while (*comment && *comment != '\x1a') {
			PIC_WrtByte(pic,*comment);
			++comment;
		}
	}
	PIC_WrtByte(pic,'\x1a');
	PIC_WrtByte(pic,'\0');

	PIC_WrtWord(pic,pic->typ);
	PIC_WrtWord(pic,pic->colbit);
	PIC_WrtWord(pic,pic->xsize);
	PIC_WrtWord(pic,pic->ysize);

	if ((pic->typ & 0x0f) == 0x0f) {	/* TYPE 15 のとき */

		PIC_WrtWord(pic,pic->xstart);
		PIC_WrtWord(pic,pic->ystart);
		PIC_WrtByte(pic,pic->asp1);
		PIC_WrtByte(pic,pic->asp2);

		if (pic->colbit <= 8) {
			int i,c,n;
			if (palbit > 8 || palbit <= 0) {
				palbit = 8;
			}
			PIC_WrtByte(pic, palbit);
			c = 0x01 << pic->colbit;
			n = 8 - palbit;
			if (rgb == NULL) {
				printf("palette data not found!\n");
			}
			for (i = 0; i < c; i++) {
				PIC_WrtBits(pic, palbit, rgb[i*3+1] >> n);
				PIC_WrtBits(pic, palbit, rgb[i*3+0] >> n);
				PIC_WrtBits(pic, palbit, rgb[i*3+2] >> n);
			}
		}

	} else if (x68kflg && (pic->colbit == 4 || pic->colbit == 8)) {
		int i,c;
		WORD n;
		if (palbit > 8 || palbit <= 0) {
			palbit = 8;
		}
		n = 0x01 << pic->colbit;
		if (rgb == NULL) {
			printf("palette data not found!\n");
		}
		for (i = 0; i < n; i++) {
			c =(((rgb[i*3+1]&0xF8)<<8)
			  | ((rgb[i*3+0]&0xF8)<<3)
			  | ((rgb[i*3+2]&0xF8)>>2));
			PIC_WrtWord(pic, c);
		}
	}
	if (pic->colbit >= 12 && pic->colbit <= 16) {
		pic->tmplin = calloc_m(pic->xsize,3);
	}
    /*PIC_FlushBuf(pic);*/
	return pic;
}


/*-------------------------------------
	color							   
-------------------------------------*/

static void PIC_InitC7t(PIC *pic)
	/* 128個の色キャッシュ・テーブルの初期化 */
{
	int i;

	pic->c7t = (PIC_C7TBL *) calloc_m(128, sizeof(PIC_C7TBL));
	if (pic->c7t == NULL){
		exit(1);
	}
	for (i = 0; i < 127; i++) {
		pic->c7t[i].col = 0;
		pic->c7t[i+1].lft = i;
		pic->c7t[i].rig = i+1;
	}
	pic->c7t[0].lft = 127;
	pic->c7t[127].rig = 0;
	pic->c7p = 0;
	return;
}

static void PIC_WrtCol24(PIC *pic, PIXEL col)
	/* 15,16,24 bits-color */
{
	int i;

	for (i = 0; i < 128; i++) {
		if (col == pic->c7t[i].col) {
			PIC_WrtBit(pic,1);
			PIC_WrtBits(pic,7,i);
			if (i != pic->c7p) {
				pic->c7t[pic->c7t[i].rig].lft = pic->c7t[i].lft;
				pic->c7t[pic->c7t[i].lft].rig = pic->c7t[i].rig;
				pic->c7t[pic->c7t[pic->c7p].rig].lft = i;
				pic->c7t[i].rig = pic->c7t[pic->c7p].rig;
				pic->c7t[pic->c7p].rig = i;
				pic->c7t[i].lft = pic->c7p;
				pic->c7p = i;
			}
			if (PIC_wrtDebFlg == 2) {
				printf("p[%u]\n",col);
			}
			return;
		}
	}
	PIC_WrtBit(pic,0);
	PIC_WrtBits(pic,pic->colbit,col);
	pic->c7p = pic->c7t[pic->c7p].rig;
	pic->c7t[pic->c7p].col = col;
	if (PIC_wrtDebFlg == 2) {
		printf("p24[%u]\n",col);
	}
	return;
}


static void PIC_WrtCol8(PIC *pic, PIXEL col)
	/* 4,8 bits-color */
{
	PIC_WrtBits(pic,pic->colbit,col);
	return;
}


static PIXEL PIC_GetPix4(PIC *pic, int x, int y)
{
	if (x & 0x01) {
		return pic->plin[y][x>>1] & 0x0f;
	} else {
		return pic->plin[y][x>>1] >> 4;
	}
}

static PIXEL PIC_GetPix8(PIC *pic, int x, int y)
{
	return pic->plin[y][x];
}

static PIXEL PIC_GetPix16(PIC *pic, int x, int y)
{
	return *(WORD *)(pic->plin[y] + x*2);
}

static PIXEL PIC_GetPix24(PIC *pic, int x, int y)
{
	return 	 (PIXEL)pic->plin[y][x*3+0] * 0x10000L
			+(PIXEL)pic->plin[y][x*3+1]*0x100
			+(PIXEL)pic->plin[y][x*3+2];
}


void PIC_InitWrtCol(PIC *pic)
{
	switch (pic->colbit) {
	case 4:
		pic->getPix = PIC_GetPix4;
		pic->wrtCol = PIC_WrtCol8;
		break;
	case 8:
		pic->getPix = PIC_GetPix8;
		pic->wrtCol = PIC_WrtCol8;
		break;
	case 12:
	case 15:
	case 16:
		pic->getPix = PIC_GetPix16;
		pic->wrtCol = PIC_WrtCol24;
		break;
	case 24:
		pic->getPix = PIC_GetPix24;
		pic->wrtCol = PIC_WrtCol24;
		break;
	default:
		printf("PRGERR:PIC_InitWrtCol\n");
		exit(1);
	}
}


/*-------------------------*/
/*-------------------------*/

static char PIC_Cmask[8] = {0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01};

#define PIC_ChkChain(pic, x, y)	\
	(pic->cmap[(int)(y)*(int)(pic->sizeX8) + (int)((x)>>3)] & PIC_Cmask[(x) & 0x07])

#define PIC_MrkChain(pic,x,y) \
	(pic->cmap[(int)(y)*(int)(pic->sizeX8) + (int)((x)>>3)]|= PIC_Cmask[(x) & 0x07])

static void PIC_MarkChains(PIC *pic, int x, int y, PIXEL col)
{
	int i,n,d;

	for (i = 0,y++;i < pic->plinCnt-1 && y < pic->ysize; i++,y++) {
		if (PIC_ChkChain(pic, x, y)==0
				&& col == (*pic->getPix)(pic,x,y)
				&& (x == 0 || col != (*pic->getPix)(pic,x-1,y) )) {
			n = 2;	d = 0x02;
		} else if (x+1 < pic->xsize
				&& PIC_ChkChain(pic,x+1,y)==0
				&& col == (*pic->getPix)(pic,x+1,y)
				&& col != (*pic->getPix)(pic,x,y) ) {
			n = 2;	d = 0x03;	x=x+1;
		} else if (x-1 >= 0
				&& PIC_ChkChain(pic,x-1,y)==0
				&& col == (*pic->getPix)(pic,x-1,y)
				&& (x <= 1 || col != (*pic->getPix)(pic,x-2,y) )) {
			n = 2;	d = 0x01;	x=x-1;
		} else if ((x+2 < pic->xsize)
				&& PIC_ChkChain(pic,x+2,y)==0
				&& col == (*pic->getPix)(pic,x+2,y)
				&& col != (*pic->getPix)(pic,x+1,y) ) {
			n = 4;	d = 0x03;	x=x+2;
		} else if ((x - 2 >= 0)
				&& PIC_ChkChain(pic,x-2,y)==0
				&& col == (*pic->getPix)(pic,x-2,y)
				&& (x <= 2 || col != (*pic->getPix)(pic,x-3,y) )) {
			n = 4;	d = 0x02;	x=x-2;
		} else {
			break;
		}
		if (i == 0) {
			PIC_WrtBit(pic,1);
		}
		PIC_MrkChain(pic, x, y);
		PIC_WrtBits(pic, n, d);
		if (PIC_wrtDebFlg == 2) {
			printf("-(%d,%d)",x,y);
		}
	}
	if (PIC_wrtDebFlg == 2) {
		printf("\nchainLine %d\n",i);
	}
	if (i) {
		PIC_WrtBits(pic, 3, 0x00);
	} else {
		PIC_WrtBit(pic, 0);
	}
}

void PIC_pix24to16(int colbit, char *p, char *buf, int xsize)
{
	int i;

	switch(colbit) {
	case 12:
		for (i = 0; i < xsize; i++, p+=3, buf+=2) {
			*(WORD *)buf = ((WORD)(p[0]&0xf0) << (8-3))
						   |((WORD)(p[1]&0xf0) << (4-3))
						   |(p[2]>>4);
		}
		break;
	case 15:
		for (i = 0; i < xsize; i++, p+=3, buf+=2) {
			*(WORD *)buf = ((WORD)(p[0]&0xf8) << (10-3))
						   |((WORD)(p[1]&0xf8) << (5-3))
						   |(p[2]>>3);
		}
		break;
	case 16:
		for (i = 0; i < xsize; i++, p+=3, buf+=2) {
			*(WORD *)buf =  ((WORD)(p[0]&0xf8) << (11-3))
						   |((WORD)(p[1]&0xf8) << (6-3))
						   |((p[2]&0xf8) >> (3-1))
						   |((p[0]&0x04) >> 2);
		}
		break;
	}
}

void PIC_PutLines(PIC *pic, void *glt, PIC_FUNC_GETLINE funcGetLine)
{
	int x,y,yy,exitFlg;
	int len;
	PIXEL col,col1,col2;

	PIC_InitC7t(pic);
  #if 0
	if (sizeof(uint) <= 2) {
		if ((int)pic->xsize*pic->ysize >= 8L * 0x10000L - 1L) {
			printf("画像の総画素数が約510Ｋﾄﾞｯﾄ未満でないと変換できない\n");
			exit(1);
		}
	}
  #endif
	pic->sizeX8 = (pic->xsize+7)/8;
	pic->cmap = (char*) calloc_m(pic->sizeX8,pic->ysize);
	pic->plin = (char**) calloc_m(pic->ysize, sizeof(char*));
	if(pic->cmap == NULL || pic->plin == NULL) {
		goto ERR;
	}
	x = pic->xsize;
	switch(pic->colbit) {
	case 4:
		x += (x & 0x01);
		x >>= 1;
	case 8:
		break;
	case 12:
	case 15:
	case 16:
		x += x;
		break;
	default:
		x *= 3;
	}
	pic->plinCnt = pic->ysize;
	for (y = 0; y < pic->ysize; y++) {
	  #if 0 /*#ifdef FAR_ALLOC*/
		pic->plin[y] = farcalloc(x, 1);
	  #else
		pic->plin[y] = calloc(x, 1);
	  #endif
		if (pic->plin[y] == NULL) {
			if (y <= 3) {
				goto ERR;
			}
			printf("Slightly not enough memory? compression rate will drop (%d lines ensured)\n",y);
			pic->plinCnt = y;
			break;
		}
	}
	for (;y < pic->ysize;y++) {
		pic->plin[y] = pic->plin[y % pic->plinCnt];
	}

	for (yy = 0;yy < pic->plinCnt;yy++) {
		if (pic->tmplin) {
			(*funcGetLine)(glt, pic->tmplin);
			PIC_pix24to16(pic->colbit, pic->tmplin, pic->plin[yy],pic->xsize);
		} else {
			(*funcGetLine)(glt, pic->plin[yy]);
		}
		if (PIC_wrtDebFlg == 2) {
			printf("\r<%d>",yy);
		}
	}
	/*yy = pic->plinCnt;*/
	
	y = x = 0;
	len = 1;
	PIC_WrtLen(pic,len);
	exitFlg = 0;
	do {
		col1 = col = (*pic->getPix)(pic,x,y);
		(*pic->wrtCol)(pic,col);
		PIC_MarkChains(pic,x,y,col);
		len = 0;
		for (; ;) {
			len++;
			if (++x >= pic->xsize) {
				x = 0;
				// printf("\r[%d]",y);
				if (++y >= pic->ysize) {
					exitFlg = 1;
					break;
				} else if (yy < pic->ysize) {
					if (pic->tmplin) {
						(*funcGetLine)(glt, pic->tmplin);
						PIC_pix24to16(pic->colbit,pic->tmplin,pic->plin[yy],pic->xsize);
					} else {
						(*funcGetLine)(glt, pic->plin[yy]);
					}
					if (PIC_wrtDebFlg == 2) {
						printf("<%d>\n",yy);
					}
					++yy;
				}
			}
			col2 = (*pic->getPix)(pic,x,y);
			if (col != col2) {
				if (PIC_ChkChain(pic,x,y)) {
					col = col2;
					if (PIC_wrtDebFlg == 2) {
						printf("q[%06lx] (%d,%d)\n",(int)col,x,y);
					}
				} else {
					break;
				}
			}
		}
		if (PIC_wrtDebFlg == 2) {
			printf("col=%06lx\n", (int)col1);
			printf("len=%ld\n",len);
		}
		PIC_WrtLen(pic,len);
	} while (exitFlg == 0);
	PIC_WrtBits(pic,7,0L);
	PIC_FlushBuf(pic);

	/* 取得したメモリを解放 */
	for (y = 0; y < pic->plinCnt; y++) {
		if (pic->plin[y]) {
			free(pic->plin[y]);
			pic->plin[y] = NULL;
		}
	}
	free(pic->plin);
	pic->plin = NULL;
	free((char far *)pic->cmap);
	pic->cmap = NULL;

	return;

  ERR:
	printf("Not enough memory\n");
	exit(1);
}


/*---------------------------------------------------------------------------*/
/*							B	M	P	入	力								 */
/*---------------------------------------------------------------------------*/
/*module BMP*/
/* export BMP, BMP_Open, BMP_CloseR, BMP_GetLine */

#define	BMP_RDBUFSIZ	0x4000

typedef struct BMP_T {
	FILE *fp;
	char *name;
	int  colbit;
	int  xsize,ysize;
	int  xstart,ystart;
	int xresol, yresol;
	char *rgb;
/* private: */
	int pdataOfs;
	int  lineSize;
  #if 1
	char *linebuf;
  #endif
} BMP;

#define BMPHDR_SIZE (14+40)

static void BMP_Free(BMP *bmp)
{
	if (bmp->fp) {
		fclose(bmp->fp);
	}
	if (bmp->name) {
		free(bmp->name);
	}
	if (bmp->rgb) {
		free(bmp->rgb);
	}
  #if 1
	if (bmp->linebuf) {
		free(bmp->linebuf);
	}
  #endif
	free(bmp);
}

void BMP_CloseR(BMP *bmp)
{
	BMP_Free(bmp);
}

static int BMP_GetB(BMP* bmp)
{
	int c;

	c = fgetc(bmp->fp);
	if (c < 0) {
		printf("There is an error in the input file\n");
		exit(1);
	}
	return c;
}

static WORD BMP_GetW(BMP* bmp)
{
	WORD w;
	w = BMP_GetB(bmp);
	return w + BMP_GetB(bmp)*0x100;
}

static DWORD BMP_GetD(BMP* bmp)
{
	DWORD d;
	d = BMP_GetW(bmp);
	return d + BMP_GetW(bmp)*0x10000L;
}


#if 0
static char BMP_dfltRGB[48] = {
	0x00,0x00,0x00,	/*黒*/
	0x80,0x00,0x00,	/*暗赤*/
	0x00,0x80,0x00,	/*暗緑*/
	0x80,0x80,0x00,	/*暗黄*/
	0x00,0x00,0x80,	/*暗青*/
	0x80,0x00,0x80,	/*暗紫*/
	0x00,0x80,0x80,	/*暗水*/
	0x80,0x80,0x80,	/*暗白(灰)*/
	0xC0,0xC0,0xC0,	/*薄白*/
	0xFF,0x00,0x00,	/*赤*/
	0x00,0xFF,0x00,	/*緑*/
	0xFF,0xFF,0x00,	/*黄*/
	0x00,0x00,0xFF,	/*青*/
	0xF0,0x00,0xFF,	/*紫*/
	0x00,0xFF,0xFF,	/*水*/
	0xFF,0xFF,0xFF	/*白*/
};
#endif

BMP *BMP_Open(char *name)
{
	BMP *bmp;
	int i,col;
  #if 0
	int fsiz, rsv;
  #endif

	bmp = calloc_m(1,sizeof(BMP));
	if (bmp == NULL) {
		return NULL;
	}
	bmp->name = strdup(name);
	bmp->fp = fopen_m(name,"rb+");
	if (bmp->fp == NULL) {
		goto ERR;
	}
	if (BMP_GetB(bmp) != 'B' || BMP_GetB(bmp) != 'M') {
		printf("BMP file ID marker incorrect (should be 'BM')\n");
		goto ERR;
	}
	/*fsiz =*/ BMP_GetD(bmp);	/* file size */
	/*rsv  =*/ BMP_GetD(bmp);	/* rsv */
	bmp->pdataOfs = BMP_GetD(bmp);
	if (BMP_GetD(bmp) != 40L) {
		printf("header size is not 40 bytes\n");
		goto ERR_HDR;
	}
	bmp->xsize = (int)BMP_GetD(bmp);
	bmp->ysize = (int)BMP_GetD(bmp);
	if (bmp->xsize <= 0 || bmp->ysize <= 0
		/*|| bmp->xstart < 0 || bmp->ystart < 0*/) {
		printf("something is wrong with the image size (%d*%d)\n",	bmp->xsize, bmp->ysize);
		goto ERR;
	}
	if (BMP_GetW(bmp) != 1) {	/* plane */
		printf("error: more than one plane\n");
		goto ERR_HDR;
	}
	bmp->colbit = BMP_GetW(bmp);
	if (bmp->colbit == 4) {
		bmp->lineSize = (bmp->xsize+1) >> 1;
	} else if (bmp->colbit == 8) {
		bmp->lineSize = bmp->xsize;
	} else {
		bmp->lineSize = bmp->xsize * 3;
	}
	if (bmp->lineSize % 4) {
		bmp->lineSize >>= 2;
		bmp->lineSize++;
		bmp->lineSize <<= 2;
	}
  #if 1
	bmp->linebuf = calloc_m(bmp->lineSize,1);
  	if (bmp->linebuf == NULL) {
  		goto ERR;
  	}
  #endif

	if (BMP_GetD(bmp) != 0L) {	/* comp. mode */
		printf("BMP is compressed\n");
		goto ERR_HDR;
	}
	if (BMP_GetD(bmp) != 0L) {	/* comp. size */
	  #if 0
		printf("圧縮bmpでないのに、圧縮時サイズが設定されている?!\n");
		/*goto ERR_HDR;*/
  	  #endif
	}
	bmp->xresol = BMP_GetD(bmp);
	bmp->yresol = BMP_GetD(bmp);
	/* col = (int)*/BMP_GetD(bmp);	/* palette count */
	BMP_GetD(bmp);	/* col. impo */
  #if 0
	if (col) {
  #endif
		col = 0x01 << bmp->colbit;
		bmp->rgb = calloc_m(256,3);
		if (bmp->rgb == NULL) {
			goto ERR;
		}
		for (i = 0; i < col; i++) {
			bmp->rgb[i*3+2] = BMP_GetB(bmp);	/* B */
			bmp->rgb[i*3+1] = BMP_GetB(bmp);	/* G */
			bmp->rgb[i*3+0] = BMP_GetB(bmp);	/* R */
			BMP_GetB(bmp);						/* A */
		}
  #if 0
	} else if (bmp->colbit <= 8) {
		bmp->rgb = calloc_m(256,3);
		if (bmp->rgb == NULL) {
			goto ERR;
		}
		if (bmp->colbit == 2) {
			bmp->rgb[3+0] = bmp->rgb[3+1] = bmp->rgb[3+2] = 0xff;
		} else if (bmp->colbit == 4) {
			memcpy(bmp->rgb, BMP_dfltRGB,48);
		} else {
			printf("パレット無の256色BMPには対応していません\n");
		}
	}
  #endif

	if (fseek(bmp->fp, bmp->pdataOfs + (int)bmp->lineSize * bmp->ysize,
		SEEK_SET)){
		printf("seek error\n");
		goto ERR;
	}
	fseek(bmp->fp, -bmp->lineSize, SEEK_CUR);
	/*printf("%s\t%d*%d %dbitcol col=%d siz=%ld  rsv=%lx\n",bmp->name,bmp->xsize,bmp->ysize,bmp->colbit,col,fsiz,rsv);*/
	return bmp;

  ERR_HDR:
	printf("the header does not match\n");
  ERR:
	BMP_Free(bmp);
	return NULL;
}

/*----------------------------------*/

void BMP_GetLine(BMP *bmp, char *buf)
	/* PIC_PutLines から呼ばれることになる関数 */
{
	int i;

	fread(bmp->linebuf, 1, bmp->lineSize, bmp->fp);
	if (bmp->colbit == 4) {
		memcpy(buf, bmp->linebuf, (bmp->xsize+1) >> 1);
	} else if (bmp->colbit == 8) {
		memcpy(buf, bmp->linebuf, bmp->xsize);
	} else if (bmp->colbit == 24) {
		for (i = 0; i < bmp->xsize; i++, buf+=3) {	/* B G R -> G R B */
			char r,g,b;
			r = bmp->linebuf[i*3+2];
			g = bmp->linebuf[i*3+1];
			b = bmp->linebuf[i*3+0];
			buf[0] = g;
			buf[1] = r;
			buf[2] = b;
		}
	}
	fseek(bmp->fp, -bmp->lineSize*2, SEEK_CUR);
}

/*end BMP*/

/*---------------------------------------------------------------------------*/
/*							R G B(Q0)入 力									 */
/*---------------------------------------------------------------------------*/
/*module Q0*/
/* export Q0, Q0_Open, Q0_CloseR, Q0_GetLine */

#define	Q0_RDBUFSIZ	0x4000

typedef struct Q0_T {
	FILE *fp;
	char *name;
	int  colbit;
	int  xsize,ysize;
	int  xstart,ystart;
	int  asp1, asp2;
	char *rgb;
/* private: */
  #if 0
	char *linebuf;
  #endif
} Q0;

static void Q0_Free(Q0 *q0p)
{
	if (q0p->fp) {
		fclose (q0p->fp);
	}
	if (q0p->name) {
		free(q0p->name);
	}
  #if 0
	if (q0p->linebuf) {
		free(q0p->linebuf);
	}
  #endif
	if (q0p->rgb) {
		free(q0p->rgb);
	}
	free(q0p);
}

void Q0_CloseR(Q0 *q0p)
{
	Q0_Free(q0p);
}

Q0 *Q0_Open(char *name)
{
	FILE *fp;
	char buf[FNAMESIZE+2];
	Q0 *q0p;

	q0p = calloc_m(1,sizeof(Q0));
	if (q0p == NULL) {
		return NULL;
	}
	q0p->colbit= 24;
	q0p->xsize = 640;
	q0p->ysize = 400;
	q0p->xstart = 0;
	q0p->ystart = 0;
	q0p->asp1  = 1;
	q0p->asp2  = 1;
	q0p->rgb   = NULL;
	q0p->name  = strdup(name);

	/*falファイル出力 */
	FIL_ChgExt(strcpy(buf,name),"FAL");
	fp = fopen(buf,"r");
	if (fp) {
		printf("RGB(Q0) information file:%s\n",buf);
		fgets(buf,FNAMESIZE,fp);
	} else {
		FIL_ChgExt(strcpy(buf,name),"IPR");
		fp = fopen(buf,"r");
		if (fp) {
			printf("RGB(Q0) information file:%s\n",buf);
		}
	}
	if (fp) {
		fgets(buf,FNAMESIZE,fp);
		sscanf(buf,"%d %d %d %d",
			&q0p->xsize,&q0p->ysize,&q0p->xstart,&q0p->ystart);
	}
	if (q0p->xsize <= 0 || q0p->ysize <= 0
		|| q0p->xstart < 0 || q0p->ystart < 0) {
		printf("there is something wrong with the contents of the information file(%d,%d,%d,%d)\n",
			q0p->xsize,q0p->ysize,q0p->xstart,q0p->ystart);
		exit(1);
	}
	fclose(fp);

	/*q0ファイル オープン*/
	q0p->name = strdup(name);
	q0p->fp = fopen_m(name,"rb");
	if (q0p->fp == NULL) {
		goto ERR;
	}
	setvbuf(q0p->fp,NULL, _IOFBF, Q0_RDBUFSIZ);
  #if 0
	q0p->linebuf = calloc_m(q0p->xsize,3);
	if (q0p->linebuf == NULL) {
		goto ERR;
	}
  #endif
	return q0p;

  ERR:
	Q0_Free(q0p);
	return NULL;
}

/*----------------------------------*/

void Q0_GetLine(Q0 *q0p, char *buf)
	/* PIC_PutLines から呼ばれることになる関数 */
{
	int i;

	fread(buf, 3, q0p->xsize, q0p->fp);
	for (i = 0; i < q0p->xsize; i++, buf+=3) {	/* R G B -> G R B */
		char r,g,b;
		r = buf[0];
		g = buf[1];
		b = buf[2];
		buf[0] = g;
		buf[1] = r;
		buf[2] = b;
	}
}

/*end Q0*/

/*---------------------------------------------------------------------------*/
/*							P	M	T	入	力								 */
/*---------------------------------------------------------------------------*/
/*module PMT*/
/* export PMT, PMT_Open, PMT_CloseR, PMT_GetLine */

#define	PMT_RDBUFSIZ	0x4000
#define PMT_HDRSIZ		(64)
#define PMT_COMMENTSIZ	0x2000


typedef struct PMT_T {
	FILE *fp;
	char *name;
	int  colbit;
	int  xsize,ysize;
	int  xstart,ystart;
	int  xasp,yasp;
	WORD ftim,fdat;
	char *rgb;
	char *comment;
	char artist[20];
/* private: */
	int  lineSize;
  #if 1
	char *linebuf;
  #endif
} PMT;

static void PMT_Free(PMT *pmt)
{
	if (pmt->fp) {
		fclose(pmt->fp);
	}
	if (pmt->name) {
		free(pmt->name);
	}
	if (pmt->rgb) {
		free(pmt->rgb);
	}
  #if 1
	if (pmt->linebuf) {
		free(pmt->linebuf);
	}
  #endif
	free(pmt);
}

void PMT_CloseR(PMT *pmt)
{
	PMT_Free(pmt);
}

static int PMT_GetB(PMT* pmt)
{
	int c;

	c = fgetc(pmt->fp);
	if (c < 0) {
		printf("there is an error in the input PMT file\n");
		exit(1);
	}
	return c;
}

static WORD PMT_GetW(PMT* pmt)
{
	WORD w;
	w = PMT_GetB(pmt);
	return w + PMT_GetB(pmt)*0x100;
}

static DWORD PMT_GetD(PMT* pmt)
{
	DWORD d;
	d = PMT_GetW(pmt);
	return d + PMT_GetW(pmt)*0x10000L;
}


PMT *PMT_Open(char *name)
{
	PMT *pmt;
	int i,c;
	int comment;

	pmt = calloc_m(1,sizeof(PMT));
	if (pmt == NULL) {
		return NULL;
	}
	pmt->name = strdup(name);
	pmt->fp = fopen_m(name,"rb+");
	if (pmt->fp == NULL) {
		goto ERR;
	}
	setvbuf(pmt->fp,NULL, _IOFBF, PMT_RDBUFSIZ);

	/* ヘッダ読み込み */
	if (PMT_GetB(pmt) != 'P' || PMT_GetB(pmt) != 'm') {
		printf("PM file ID is incorrect (should be 'Pm')\n");
		goto ERR;
	}
	pmt->colbit = PMT_GetB(pmt);
	/* pmt->flags = */ PMT_GetB(pmt);
	pmt->xsize  = PMT_GetW(pmt);
	pmt->ysize  = PMT_GetW(pmt);
	if (pmt->xsize <= 0 || pmt->ysize <= 0) {
		printf("something is wrong with the image size(%d*%d)\n",	pmt->xsize, pmt->ysize);
		goto ERR;
	}
	pmt->xstart = PMT_GetW(pmt);
	pmt->ystart = PMT_GetW(pmt);
	/* pmt->bcol = */ PMT_GetW(pmt);
	/* pmt->rsv1 = */ PMT_GetW(pmt);
	pmt->xasp = PMT_GetW(pmt);
	pmt->yasp = PMT_GetW(pmt);
	comment = PMT_GetD(pmt);
	for(i = 0; i < 19; i++) {
		pmt->artist[i] = PMT_GetB(pmt);
	}
	/* pmt->timeSec1 = */ PMT_GetB(pmt);
	pmt->ftim = PMT_GetW(pmt);
	pmt->fdat = PMT_GetW(pmt);
	for (i = 0; i < 16; i++) {	/* 予約領域スキップ */
		PMT_GetB(pmt);
	}

	/* コメント読込み */
	if (comment) {
		char *p;
		fseek(pmt->fp, comment, SEEK_SET);
		pmt->comment = p = malloc(PMT_COMMENTSIZ);
		if (p == NULL) {
			goto ERR;
		}
		i = 0;
		for (;;) {
			c = fgetc(pmt->fp); /*c = PMT_GetB(pmt);*/
			if (c <= 0 || c == 0x1a) { /* EOF ならコメント終了 */
				break;
			} else if (c && i < PMT_COMMENTSIZ-2) {
				*p++ = (char)c;
				i++;
			}
		}
		*p = '\0';
		realloc(pmt->comment, i+1); /*ｺﾒﾝﾄﾊﾞｯﾌｧを実際に読込んだサイズにする*/
		fseek(pmt->fp, PMT_HDRSIZ, SEEK_SET);
	}

	/* パレット読み込み */
	if (pmt->colbit <= 8) {
		pmt->rgb = calloc_m(3,256);
		if (pmt->rgb == NULL) {
			goto ERR;
		}
		for (i = 0; i < 256; i++) {
			pmt->rgb[i*3+0] = PMT_GetB(pmt);	/* B */
			pmt->rgb[i*3+1] = PMT_GetB(pmt);	/* G */
			pmt->rgb[i*3+2] = PMT_GetB(pmt);	/* R */
		}
	}
	/* 作業バッファ確保 */
	if (pmt->colbit <= 4) {
		pmt->lineSize = pmt->xsize * 1;
		pmt->linebuf = calloc_m(pmt->lineSize,1);
		if (pmt->linebuf == NULL) {
			goto ERR;
		}
	} else if (pmt->colbit <= 8) {
		;
	} else {
		pmt->lineSize = pmt->xsize * 3;
		pmt->linebuf = calloc_m(pmt->lineSize,1);
		if (pmt->linebuf == NULL) {
			goto ERR;
		}
	}
	return pmt;

  ERR:
	PMT_Free(pmt);
	return NULL;
}

/*----------------------------------*/

void PMT_GetLine(PMT *pmt, char *buf)
	/* PIC_PutLines から呼ばれることになる関数 */
{
	if (pmt->colbit <= 4) {
		int i;
		fread(pmt->linebuf, 1, pmt->xsize, pmt->fp);
		for (i = 0; i < pmt->xsize; i+=2) {
			buf[i>>1] = ((pmt->linebuf[i]&0xf) << 4)|(pmt->linebuf[i+1]&0xf);
		}
	} else if (pmt->colbit <= 8) {
		fread(buf, 1, pmt->xsize, pmt->fp);
	} else {
		int i;
		fread(pmt->linebuf, 1, pmt->lineSize, pmt->fp);
		for (i = 0; i < pmt->xsize; i++, buf+=3) {	/* R G B -> G R B */
			buf[1] = pmt->linebuf[i*3+0];	/* R */
			buf[0] = pmt->linebuf[i*3+1];	/* G */
			buf[2] = pmt->linebuf[i*3+2];	/* B */
		}
	}
}

/*end PMT*/


/*---------------------------------------------------------------------------*/
/*							D	J	P	入	力								 */
/*---------------------------------------------------------------------------*/
/*module DJP*/
/* export DJP, DJP_Open, DJP_CloseR, DJP_GetLine */

#define	DJP_RDBUFSIZ	0x4000
#define DJP_HDRSIZ		(12)


typedef struct DJP_T {
	FILE *fp;
	char *name;
	int  colbit;
	int  xsize,ysize;
	char *rgb;
	char id[6];
/* private: */
	int  lineSize;
  #if 1
	char *linebuf;
  #endif
} DJP;

static void DJP_Free(DJP *djp)
{
	if (djp->fp) {
		fclose(djp->fp);
	}
	if (djp->name) {
		free(djp->name);
	}
	if (djp->rgb) {
		free(djp->rgb);
	}
  #if 1
	if (djp->linebuf) {
		free(djp->linebuf);
	}
  #endif
	free(djp);
}

void DJP_CloseR(DJP *djp)
{
	DJP_Free(djp);
}

static int DJP_GetB(DJP* djp)
{
	int c;

	c = fgetc(djp->fp);
	if (c < 0) {
		printf("there is an error in the input DJP file\n");
		exit(1);
	}
	return c;
}

static WORD DJP_GetW(DJP* djp)
{
	WORD w;
	w = DJP_GetB(djp);
	return w + DJP_GetB(djp)*0x100;
}

DJP *DJP_Open(char *name)
{
	DJP *djp;
	int i;

	djp = calloc_m(1,sizeof(DJP));
	if (djp == NULL) {
		return NULL;
	}
	djp->name = strdup(name);
	djp->fp = fopen_m(name,"rb+");
	if (djp->fp == NULL) {
		goto ERR;
	}
	setvbuf(djp->fp,NULL, _IOFBF, DJP_RDBUFSIZ);

	/* ヘッダ読み込み */
	for (i = 0; i < 6; i++) {
		djp->id[i] = DJP_GetB(djp);
	}
	if (memcmp(djp->id,"DJ505J",6) != 0) {
		printf("DJP file ID is incorrect (should be 'DJ505J')\n");
		goto ERR;
	}
	djp->xsize  = DJP_GetW(djp);
	djp->ysize  = DJP_GetW(djp);
	if (djp->xsize <= 0 || djp->ysize <= 0) {
		printf("something is wrong with the image size (%d*%d)\n",	djp->xsize, djp->ysize);
		goto ERR;
	}
	djp->colbit = DJP_GetW(djp);

	/* パレット読み込み */
	if (djp->colbit == 0) {
		djp->rgb = calloc_m(3,256);
		if (djp->rgb == NULL) {
			goto ERR;
		}
		for (i = 0; i < 256; i++) {
			djp->rgb[i*3+0] = DJP_GetB(djp);	/* B */
			djp->rgb[i*3+1] = DJP_GetB(djp);	/* G */
			djp->rgb[i*3+2] = DJP_GetB(djp);	/* R */
		}
	}
	/* 作業バッファ確保 */
	if (djp->colbit <= 8) {
		;
	} else {
		djp->lineSize = djp->xsize * 3;
		djp->linebuf = calloc_m(djp->lineSize,1);
		if (djp->linebuf == NULL) {
			goto ERR;
		}
	}
	return djp;

  ERR:
	DJP_Free(djp);
	return NULL;
}

/*----------------------------------*/

void DJP_GetLine(DJP *djp, char *buf)
	/* PIC_PutLines から呼ばれることになる関数 */
{
	if (djp->colbit <= 8) {
		fread(buf, 1, djp->xsize, djp->fp);
	} else {
		int i;
		fread(djp->linebuf, 1, djp->lineSize, djp->fp);
		for (i = 0; i < djp->xsize; i++, buf+=3) {	/* R G B -> G R B */
			buf[1] = djp->linebuf[i*3+0];	/* R */
			buf[0] = djp->linebuf[i*3+1];	/* G */
			buf[2] = djp->linebuf[i*3+2];	/* B */
		}
	}
}

/*end DJP*/



/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
#define RF_TIF	1
#define RF_BMP	2
#define RF_RGB	3
#define RF_Q0	4
#define RF_PMT	5
#define RF_PMY	6
#define RF_DJP	7
#define RF_98	8

char gRdExt[][4] = {"*","TIF","BMP","RGB","Q0","PMT","PMY","DJP",""};

#ifdef FDATEGET
  	static int  fdateFlg = 1;	/* 元ﾌｧｲﾙの日付もコピー */
#endif

static void Resol2Asp(int xresol, int yresol, int *asp1, int *asp2)
{
	static int nn[7] = {3,5,7,11,13,17,19};
	int i;

	if (xresol == 0 || yresol == 0) {
		*asp1 = *asp2 = 1;
		return;
	}
	while (((xresol&0x01) == 0) && ((yresol&0x01) == 0)) {
		xresol >>= 1;
		yresol >>= 1;
	}
	for (i = 0; i < 7; i++) {
		while (((xresol % nn[i]) == 0) && ((yresol % nn[i]) == 0)) {
			xresol /= nn[i];
			yresol /= nn[i];
		}
	}
	if (xresol == yresol) {
		*asp1 = *asp2 = 1;
	} else if (xresol % yresol == 0) {
		*asp1 = (int)(xresol / yresol);
		*asp2 = 1;
	} else if (yresol % xresol == 0) {
		*asp1 = 1;
		*asp2 = (int)(yresol / xresol);
	} else if (xresol <= 255 && yresol <= 255) {
		*asp1 = (int)yresol;
		*asp2 = (int)xresol;
	} else if (xresol && yresol) {
		/* ここからは、縦横比の値に誤差が生じる...*/
		if (xresol >= yresol) {
			*asp1 = (int)(xresol / yresol);
			if (*asp1 > 2) {
				*asp2 = 1;
			} else {
				*asp1 = (int)(xresol*100/yresol);
				*asp2 = 100;
			}
		} else {
			*asp2 = (int)(yresol / xresol);
			if (*asp2 > 2) {
				*asp1 = 1;
			} else {
				*asp2 = (int)(yresol*100/xresol);
				*asp1 = 100;
			}
		}
	} else {
		*asp1 = *asp2 = 0;
	}
	if (*asp1 > 255) {
		*asp1 = 255;
	}
	if (*asp2 > 255) {
		*asp2 = 255;
	}
}

static void PriInfo(PIC *pic, int rdFmt,char *srcName, char *dstName)
	/* 変換メッセージ */
{
	int xstart,ystart;

	xstart = pic->xstart; if (xstart < 0) xstart = 0;
	ystart = pic->ystart; if (ystart < 0) ystart = 0;
	printf("[%3s->PIC] %s -> %s\n",gRdExt[rdFmt], srcName, dstName);
	printf("           size %4d*%-4d %2dbit color  Aspect=%d:%-2d Type=%02x  (%d,%d)-(%d,%d)\n",
		pic->xsize,pic->ysize,pic->colbit, pic->asp1, pic->asp2, pic->typ,
		xstart,ystart, xstart+pic->xsize-1, ystart+pic->ysize-1);
}


int ConvPdata(char *srcName, char *dstName,int rdFmt, int x68kflg,
				 int colbit,int palbit, int asp1, int asp2)
{
  #ifdef FDATEGET
	static unsigned fdat, ftim;
  #endif
	PIC *pic;

	/* 入力フォーマットの指定がなければ、拡張子より判別 */
	if (rdFmt <= 0) {
		char *p;
		p = strrchr(srcName, '.');
		if (p) {
			++p;
			for (rdFmt = 1; gRdExt[rdFmt][0] != '\0'; rdFmt++) {
				if (strcasecmp(p,gRdExt[rdFmt]) == 0) {
					break;
				}
			}
			if (gRdExt[rdFmt][0] == '\0') {
				return 1;
			}
		}
	}
	/*---------- BMP 入力 ---------------*/
	if (rdFmt == RF_BMP) {
		BMP	*bmp;

		bmp = BMP_Open(srcName);
		if (bmp == NULL) {
			return 1;
		}
  #ifdef FDATEGET
		/* ファイル日付取得 */
		if (fdateFlg) {
			_dos_getftime(fileno(bmp->fp), &fdat, &ftim);
  		}
  #endif
		if (bmp->colbit == 1) {
			printf("binary image cannot be converted\n");
			BMP_CloseR(bmp);
			return 1;
		}
		if (bmp->colbit <= 8 || colbit == 0) {
			colbit = bmp->colbit;
		}
		if ((asp1 == 0 || asp2 == 0) && bmp->xresol && bmp->yresol) {
			Resol2Asp(bmp->xresol,bmp->yresol, &asp1, &asp2);
		}
		pic = PIC_Create(dstName,colbit,bmp->xsize,bmp->ysize,
							-1/*bmp->xstart*/,-1/*bmp->ystart*/,
							asp1,asp2,palbit,bmp->rgb,x68kflg, NULL, NULL);
		if (pic == NULL) {
			BMP_CloseR(bmp);
			return 1;
		}
		PIC_InitWrtCol(pic);
		PriInfo(pic, rdFmt,srcName,dstName);		/* 変換メッセージ */
		PIC_PutLines(pic, (void *)bmp, (PIC_FUNC_GETLINE)BMP_GetLine);
	  #ifdef FDATEGET
		if (fdateFlg) {
			_dos_setftime(fileno(pic->fp), fdat, ftim);
		}
	  #endif

		PIC_CloseW(pic);
		BMP_CloseR(bmp);

	/*---------- RGB,Q0 入力 ---------------*/
	} else if (rdFmt == RF_RGB || rdFmt == RF_Q0) {
		Q0	*q0p;

		/* Q0ファイル(ヘッダ)作成 */
		q0p = Q0_Open(srcName);
		if (q0p == NULL) {
			return 1;
		}
  #ifdef FDATEGET
		/* ファイル日付取得 */
		if (fdateFlg) {
			_dos_getftime(fileno(q0p->fp), &fdat, &ftim);
  		}
  #endif
		if (q0p->colbit <= 8 || colbit == 0) {
			colbit = q0p->colbit;
		}
		pic = PIC_Create(dstName,colbit,q0p->xsize,q0p->ysize,
							q0p->xstart,q0p->ystart,asp1,asp2,
							palbit,q0p->rgb,x68kflg, NULL, NULL);
		if (pic == NULL) {
			Q0_CloseR(q0p);
			return 1;
		}
		PIC_InitWrtCol(pic);
		PriInfo(pic, rdFmt,srcName,dstName);		/* 変換メッセージ */

		PIC_PutLines(pic, (void *)q0p, (PIC_FUNC_GETLINE)Q0_GetLine);
	  #ifdef FDATEGET
		if (fdateFlg) {
			_dos_setftime(fileno(pic->fp), fdat, ftim);
		}
	  #endif
		PIC_CloseW(pic);
		Q0_CloseR(q0p);

	/*---------- PMT 入力 ---------------*/
	} else if (rdFmt == RF_PMT || rdFmt == RF_PMY) {
		PMT	*pmt;

		pmt = PMT_Open(srcName);
		if (pmt == NULL) {
			return 1;
		}
  #ifdef FDATEGET
		/* ファイル日付取得 */
		if (fdateFlg) {
			_dos_getftime(fileno(pmt->fp), &fdat, &ftim);
  		}
  #endif
		if (colbit && pmt->colbit <= 11)
			colbit = 0;	/* 8ビット色以下では 強制 12,15,16ﾋﾞｯﾄ色はできない */
		if (colbit != 12 && colbit != 15 && colbit != 16 && colbit != 24) {
			if (pmt->colbit <= 4)		colbit = 4;
			else if (pmt->colbit <= 8)	colbit = 8;
			else if (pmt->colbit <= 15)	colbit = 15;
			else if (pmt->colbit <= 18)	colbit = 16;
			else						colbit = 24;
		}
		if ((asp1 == 0 || asp2 == 0) && pmt->xasp && pmt->yasp) {
			asp1 = pmt->xasp;
			asp2 = pmt->yasp;
		}
		pic = PIC_Create(dstName,colbit,pmt->xsize,pmt->ysize,
							pmt->xstart,pmt->ystart,asp1,asp2,palbit,
							pmt->rgb,x68kflg, pmt->comment,pmt->artist);
		if (pic == NULL) {
			PMT_CloseR(pmt);
			return 1;
		}
		PIC_InitWrtCol(pic);
		PriInfo(pic, rdFmt,srcName,dstName);		/* 変換メッセージ */
		PIC_PutLines(pic, (void *)pmt, (PIC_FUNC_GETLINE)PMT_GetLine);
	  #ifdef FDATEGET
		if (pmt->fdat) {
			fdat = pmt->fdat;
			ftim = pmt->ftim;
		}
		if (fdateFlg) {
			_dos_setftime(fileno(pic->fp), fdat, ftim);
		}
	  #endif
		PIC_CloseW(pic);
		PMT_CloseR(pmt);

	/*---------- DJP 入力 ---------------*/
	} else if (rdFmt == RF_DJP) {
		DJP	*djp;

		djp = DJP_Open(srcName);
		if (djp == NULL) {
			return 1;
		}
  #ifdef FDATEGET
		/* ファイル日付取得 */
		if (fdateFlg) {
			_dos_getftime(fileno(djp->fp), &fdat, &ftim);
  		}
  #endif
		if (djp->colbit <= 8) {
			colbit = 8;
		} else if (colbit == 0) {
			colbit = djp->colbit;
		}
		pic = PIC_Create(dstName,colbit,djp->xsize,djp->ysize, 0,0,
						asp1,asp2,palbit,djp->rgb,x68kflg, NULL, NULL);
		if (pic == NULL) {
			// PMT_CloseR((PMT*)djp); // original code
			DJP_CloseR(djp);	//fix compile error
			return 1;
		}
		PIC_InitWrtCol(pic);
		PriInfo(pic, rdFmt,srcName,dstName);		/* 変換メッセージ */
		PIC_PutLines(pic, (void *)djp, (PIC_FUNC_GETLINE)DJP_GetLine);
	  #ifdef FDATEGET
		if (fdateFlg) {
			_dos_setftime(fileno(pic->fp), fdat, ftim);
		}
	  #endif
		PIC_CloseW(pic);
		DJP_CloseR(djp);

	/*--------- その他 --------------*/
	} else {
		printf("could not succeed\n");
	}
	return 0;
}

/*---------------------------------------------------------------------------*/


void Usage(void)
{
  puts(
	"\nPICsv v0.81 Uncompressed BMP, RGB, Q0 -> PIC converter\n"
	"usage: PICsv [.FMT] [-opts] file(s)\n"
	" .FMT      input image format: .bmp .q0 .rgb .dj .pmx (.pmy)\n"
	" -o<FILE>  specify output file as FILE\n"
	" -0        generate TYPE 0 (A)PIC (4,8,15,16-bit color)\n"
	" -e        generate TYPE 15 extension PIC\n"
	" -c<N>     for a 24 bit color input, force N bit color handling. N=15,16,24 only\n"
	" -a<M:N>   force aspect ratio of input (Horiontal:Vertical) to M:N. M,N <= 255\n"
	" -pb<N>    number per bit of the palette in TYPE 15 output. N=1~8. Default is 8\n"
  #ifdef FDATEGET
	" -d[-]     copies date of input file. -d- does not copy file date\n"
  #endif
	"\n"
	"You can specify more than 1 input file (if -o flag not present)\n"
	"TYPE 0(apic) and TYPE 15(extended pic) files can be created.(TYPE 0 default)\n"
	"TYPE 0 PIC files may have 4,8,15,16 bit color\n"
	"TYPE 15 PIC files may have 8,15,16,24 bit color (16 and 4096 colors cannot be made)\n"
	"15 bit color throws away only the lower 3 bits of a 24 bit color file\n"
	"16 bit conversion behaves similarly, but the 6th bit of G is used as a brightness bit\n"
	"for rgb,q0 inputs, first look for .fal fail, if not found, then search for .ipr file\n"
	"otherwise, the image will be treated as having size 640x400\n"
	"file translated and ported to x64 by famiac\n"
  );
  exit(0);
}

#ifdef KEYBRK
void KeyBrk(int sig)
	/* Stop-Key , ctrl-c 割り込み用*/
{
	printf("Abort.\n");
	sig = 1;
	exit(sig);
}
#endif

int main(int argc, char *argv[])
{
  #ifdef DIRENTRY
	int DirEntryGet(char far *fname, char far *wildname, int fmode);
	static char nambuf[FNAMESIZE+2];
  #endif
	static char srcName[FNAMESIZE+2];
	static char dstName[FNAMESIZE+2];
	int  i,c;
	int  sw_oneFile;
	int  rdFmt;
	int  colbit;
	int  palbit;
	int  x68kflg;
	int  asp1,asp2;
	char *p;

  #ifdef KEYBRK
	signal(SIGINT,KeyBrk);
  #endif
	srcName[0] = dstName[0] = 0;
	asp1 = asp2 = palbit = colbit = sw_oneFile = 0;
	x68kflg = 1;
	rdFmt = 0;
	if (argc < 2) {
		Usage();
	}
	/* オプション読み取り */
	for (i = 1; i < argc; i++) {
		p = argv[i];
		if (*p == '.' && p[1] != '.' && p[1] != '/' && p[1] != '\\') {
			p++;
			for (rdFmt = 1; gRdExt[rdFmt][0] != '\0'; rdFmt++) {
				if (strcasecmp(p,gRdExt[rdFmt]) == 0)
					break;
			}
			if (gRdExt[rdFmt][0] == '\0') {
				printf("unknown format\n",argv[i]);
				exit(1);
			}

		} else if (*p == '-') {
			p++;
			c = toupper(*p);
			p++;
			switch (c) {
			case '?':
			case '\0':
				Usage();
				break;
			case 'Z':
				PIC_wrtDebFlg = 1;
				c = *p++;
				if (c >= '0' && c <= '9') {
					PIC_wrtDebFlg = c - '0';
				}
				break;
			case 'O':
				if (*p == 0)
					goto OPTERR;
				strncpy(dstName,p,FNAMESIZE);
				dstName[FNAMESIZE] = 0;
				sw_oneFile = 1;
				break;
			case 'A':
				if (*p == 0) {
					asp1 = asp2 = 1;
				} else {
					asp1 = (int)strtol(p,&p, 0);
					if (*p) {
						asp2 = (int)strtol(p+1,NULL,0);
					}
					if (asp1 > 255 || asp2 > 255) {
						printf("aspect ratio too large\n");
						exit(1);
					}
				}
				break;
			case 'P':
				if (*p == 'B' || *p == 'b') {
					palbit = (int)strtol(p+1,NULL,10);
					if (palbit < 1 || palbit > 8) {
						printf("N in -pbN must be from 1 to 8\n");
						exit(1);
					}
				}
				break;
			case 'C':
				c = (int)strtol(p,NULL,10);
				if (c != 12 && c != 15 && c != 16 && c != 24) {
					printf("N in -cN can only be 15 or 16\n");
					exit(1);
				}
				colbit = c;
				break;
			case '0':
				x68kflg = 1;
				break;
			case 'E':
				x68kflg = 0;
				break;
		  #if 0
			case '9':
				if (*p == '8') {
					rdFmt = RF_98;
				} else {
					goto OPTERR;
				}
				break;
		  #endif
  #ifdef FDATEGET
  			case 'D':
  				fdateFlg = 1;
  				if (*p == '-') {
  					fdateFlg = 0;
  				}
  				break;
  #endif
			default:
	  OPTERR:
				printf("option specification is incorrect [ -%s ]\n",p-1);
				exit(1);
			}
		}
	}
	/* ファイルごとの処理 */
	for (i = 1; i < argc; i++) {
		p = argv[i];
		if (*p == '-') {
			continue;
		}
		FIL_AddExt(strcpy(srcName,p), gRdExt[rdFmt]);
  	  #ifdef DIRENTRY
		if (DirEntryGet(nambuf,srcName,0) == 0) {
			strcpy(srcName,nambuf);
			do {
	  #endif
				if (sw_oneFile == 0) {
					FIL_ChgExt(strcpy(dstName, srcName), "PIC");
					c = ConvPdata(srcName,dstName,rdFmt,x68kflg,colbit,
						palbit,asp1,asp2);
				} else {	/* -o */
					FIL_AddExt(dstName, "PIC");
					c = ConvPdata(srcName,dstName,rdFmt,x68kflg,colbit,
						palbit,asp1,asp2);
					return c;
				}
	  #ifdef DIRENTRY
			} while(DirEntryGet(srcName,NULL,0) == 0);
		}
	  #endif
	}
	return 0;
}

/*
	実は実験・デバッグ関係で拡張PICで 16色, 4096色画像の作成はできるようになっ
	ているがあくまでも実験のためのものなので作成された16色,4096色 PICファイル
	をパソ通等で配布してはいけません。
	まあ、16色 PIC は まず MAG よりもかなり圧縮率悪い、し^^;
	4096色画像は、なんとなく....ま、あえて作成することはないと思うし...
*/

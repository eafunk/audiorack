/*
 * libdbi - database independent abstraction layer for C.
 * Copyright (C) 2001-2004, David Parker and Mark Tobenkin.
 * http://libdbi.sourceforge.net
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * oracle_charsets.h
 * Copyright (C) 2003-2004, Christian M. Stamgren <christian@stamgren.com>
 * http://libdbi-drivers.sourceforge.net
 *
 */

/**
 * Oracle native to IANA Charsetnames translation table.
 */
//This table needs ALOT more work, please help me!! 
static const char oracle_encoding_hash[][20] = {
	/* from , to */
	"EL8DEC","EL8DEC", //DEC 8-bit Latin/Greek 
	"EL8GCOS7","EL8GCOS7", //Bull EBCDIC GCOS7 8-bit Greek 
	"RU8BESTA","RU8BESTA", //BESTA 8-bit Latin/Cyrillic 
	"SF7ASCII","SF7ASCII", //ASCII 7-bit Finnish 
	"TR7DEC","TR7DEC", //DEC VT100 7-bit Turkish 
	"TR8DEC", "TR8DEC", //DEC 8-bit Turkish 
	"TR8EBCDIC1026","TR8EBCDIC1026", //EBCDIC Code Page 1026 8-bit Turkish  
	"TR8EBCDIC1026S","TR8EBCDIC1026", //EBCDIC Code Page 1026 Server 8-bit Turkish 
	"TR8MACTURKISH","TR8EBCDIC1026", //MAC Client 8-bit Turkish 
	"TR8PC857","IBM857", //IBM-PC Code Page 857 8-bit Turkish 
	"US7ASCII","US-ASCII", //ASCII 7-bit American 
	"WE8GCOS7","WE8GCOS7", //Bull EBCDIC GCOS7 8-bit West European 
	"YUG7ASCII","YUG7ASCII", //ASCII 7-bit Yugoslavian 
	"AL16UTF16","AL16UTF16",
	"AL32UTF8" ,"AL32UTF8" ,
	"AR8ADOS710","AR8ADOS710", //Arabic MS-DOS 710 Server 8-bit Latin/Arabic 
	"AR8ADOS710T","AR8ADOS710T", //Arabic MS-DOS 710 8-bit Latin/Arabic 
	"AR8ADOS720","AR8ADOS720", //Arabic MS-DOS 720 Server 8-bit Latin/Arabic 
	"AR8ADOS720T","AR8ADOS720T", //Arabic MS-DOS 720 8-bit Latin/Arabic 
	"AR8APTEC715",	"AR8APTEC715", //APTEC 715 Server 8-bit Latin/Arabic 
	"AR8APTEC715T","AR8APTEC715T", //APTEC 715 8-bit Latin/Arabic 
	"AR8ARABICMAC","AR8ARABICMAC", //Mac Client 8-bit Latin/Arabic 
	"AR8ARABICMACS","AR8ARABICMACS", //Mac Server 8-bit Latin/Arabic 
	"AR8ARABICMACT","AR8ARABICMACT", //Mac 8-bit Latin/Arabic 
	"AR8ASMO708PLUS","AR8ASMO708PLUS", //ASMO 708 Plus 8-bit Latin/Arabic 
	"AR8ASMO8X","AR8ASMO8X", //ASMO Extended 708 8-bit Latin/Arabic 
	"AR8EBCDIC420S","AR8EBCDIC420S", //EBCDIC Code Page 420 Server 8-bit Latin/Arabic 
	"AR8EBCDICX", "AR8EBCDICX", //EBCDIC XBASIC Server 8-bit Latin/Arabic 
	"AR8HPARABIC8T","AR8HPARABIC8T", //HP 8-bit Latin/Arabic 
	"AR8ISO8859P6","ISO-8859-6", //ISO 8859-6 Latin/Arabic 
	"AR8MSWIN1256","AR8MSWIN1256", //MS Windows Code Page 1256 8-Bit Latin/Arabic 
	"AR8MUSSAD768","AR8MUSSAD768", //Mussa'd Alarabi/2 768 Server 8-bit Latin/Arabic 
	"AR8MUSSAD768T","AR8MUSSAD768T", //Mussa'd Alarabi/2 768 8-bit Latin/Arabic 
	"AR8NAFITHA711","AR8NAFITHA711", //Nafitha Enhanced 711 Server 8-bit Latin/Arabic 
	"AR8NAFITHA711T","AR8NAFITHA711T", //Nafitha International 721 Server 8-bit Latin/Arabic 
	"AR8NAFITHA721T","AR8NAFITHA721T", //Nafitha International 721 8-bit Latin/Arabic 
	"AR8SAKHR706","AR8SAKHR706", //SAKHR 706 Server 8-bit Latin/Arabic 
	"AR8SAKHR707","AR8SAKHR707", //SAKHR 707 Server 8-bit Latin/Arabic 
	"AR8SAKHR707T","AR8SAKHR707T", //SAKHR 707 8-bit Latin/Arabic 
	"AR8XBASIC","AR8XBASIC", //XBASIC 8-bit Latin/Arabic 
	"BLT8EBCDIC1112","BLT8EBCDIC1112", //EBCDIC Code Page 1112 8-bit Baltic Multilingual 
	"BLT8EBCDIC1112S","BLT8EBCDIC1112S", //EBCDIC Code Page 1112 8-bit Server Baltic Multilingual 
	"BLT8MSWIN1257","BLT8MSWIN1257", //MS Windows Code Page 1257 8-bit Baltic 
	"BN8BSCII","BN8BSCII", //Bangladesh National Code 8-bit BSCII 
	"CH7DEC","CH7DEC", //DEC VT100 7-bit Swiss (German/French) 
	"CL8EBCDIC1025","CL8EBCDIC1025", //EBCDIC Code Page 1025 8-bit Cyrillic 
	"CL8EBCDIC1025C","CL8EBCDIC1025C", //EBCDIC Code Page 1025 Client 8-bit Cyrillic 
	"CL8EBCDIC1025R","CL8EBCDIC1025R", //EBCDIC Code Page 1025 Server 8-bit Cyrillic 
	"CL8EBCDIC1025S","CL8EBCDIC1025S", //EBCDIC Code Page 1025 Server 8-bit Cyrillic 
	"CL8EBCDIC1025X","CL8EBCDIC1025X", //	EBCDIC Code Page 1025 (Modified) 8-bit Cyrillic 
	"CL8ISOIR111","CL8ISOIR111", //ISOIR111 Cyrillic 
	"CL8KOI8R","CL8KOI8R", //RELCOM Internet Standard 8-bit Latin/Cyrillic 
	"CL8KOI8U","CL8KOI8U", //KOI8 Ukrainian Cyrillic 
	"CL8MSWIN1251","CL8MSWIN1251", //MS Windows Code Page 1251 8-bit Latin/Cyrillic 
	"D7DEC","D7DEC", //DEC VT100 7-bit German 
	"D7SIEMENS9780X","D7SIEMENS9780X", //Siemens 97801/97808 7-bit German 
	"D8BS2000","D8BS2000", //Siemens 9750-62 EBCDIC 8-bit German 
	"D8EBCDIC1141","D8EBCDIC1141", //EBCDIC Code Page 1141 8-bit Austrian German 
	"D8EBCDIC273","D8EBCDIC273", //EBCDIC Code Page 273/1 8-bit Austrian German 
	"DK7SIEMENS9780X","DK7SIEMENS9780X", //Siemens 97801/97808 7-bit Danish 
	"DK8BS2000","DK8BS2000", //Siemens 9750-62 EBCDIC 8-bit Danish 
	"DK8EBCDIC1142","DK8EBCDIC1142", //EBCDIC Code Page 1142 8-bit Danish  
	"DK8EBCDIC277","DK8EBCDIC277", //EBCDIC Code Page 277/1 8-bit Danish 
	"E7DEC","E7DEC", //DEC VT100 7-bit Spanish 
	"E7SIEMENS9780X","E7SIEMENS9780X", //Siemens 97801/97808 7-bit Spanish 
	"E8BS2000","E8BS2000", //Siemens 9750-62 EBCDIC 8-bit Spanish 
	"EE8EBCDIC870","EE8EBCDIC870", //EBCDIC Code Page 870 8-bit East European 
	"EE8EBCDIC870C","EE8EBCDIC870C", //EBCDIC Code Page 870 Client 8-bit East European 
	"EE8EBCDIC870S","EE8EBCDIC870S", //EBCDIC Code Page 870 Server 8-bit East European 
	"EEC8EUROASCI","EEC8EUROASCI", //EEC Targon 35 ASCI West European/Greek 
	"EEC8EUROPA3","EEC8EUROPA3", //EEC EUROPA3 8-bit West European/Greek 
	"EL8EBCDIC875","EL8EBCDIC875", //EBCDIC Code Page 875 8-bit Greek 
	"EL8EBCDIC875R","EL8EBCDIC875R", //EBCDIC Code Page 875 Server 8-bit Greek 
	"EL8MSWIN1253","EL8MSWIN1253", //MS Windows Code Page 1253 8-bit Latin/Greek 
	"F7DEC","F7DEC", //DEC VT100 7-bit French 
	"F7SIEMENS9780X","F7SIEMENS9780X", //Siemens 97801/97808 7-bit French 
	"F8BS2000","F8BS2000", //Siemens 9750-62 EBCDIC 8-bit French 
	"F8EBCDIC1147","F8EBCDIC1147", //EBCDIC Code Page 1147 8-bit French  
	"F8EBCDIC297","F8EBCDIC297", //EBCDIC Code Page 297 8-bit French 
	"I7DEC","I7DEC", //DEC VT100 7-bit Italian 
	"I8EBCDIC1144","I8EBCDIC1144", //EBCDIC Code Page 1144 8-bit Italian 
	"I8EBCDIC280","I8EBCDIC280", //EBCDIC Code Page 280/1 8-bit Italian 
	"IN8ISCII","IN8ISCII", //Multiple-Script Indian Standard 8-bit Latin/Indian Languages 
	"IW7IS960","IW7IS960", //Israeli Standard 960 7-bit Latin/Hebrew 
	"IW8EBCDIC1086","IW8EBCDIC1086", //EBCDIC Code Page 1086 8-bit Hebrew 
	"IW8EBCDIC424","IW8EBCDIC424", //EBCDIC Code Page 424 8-bit Latin/Hebrew 
	"IW8EBCDIC424S","IW8EBCDIC424S", //EBCDIC Code Page 424 Server 8-bit Latin/Hebrew 
	"IW8ISO8859P8","ISO-8859-8", //ISO 8859-8 Latin/Hebrew 
	"IW8MACHEBREW","IW8MACHEBREW", //Mac Client 8-bit Hebrew 
	"IW8MACHEBREWS","IW8MACHEBREWS", //Mac Server 8-bit Hebrew 
	"IW8MSWIN1255","IW8MSWIN1255", //MS Windows Code Page 1255 8-bit Latin/Hebrew 
	"IW8PC1507","IW8PC1507", //IBM-PC Code Page 1507/862 8-bit Latin/Hebrew 
	"JA16DBCS","JA16DBCS", //IBM EBCDIC 16-bit Japanese 
	"JA16EBCDIC930","JA16EBCDIC930", //IBM DBCS Code Page 290 16-bit Japanese 
	"JA16EUC","EUC-JP", //EUC 24-bit Japanese 
	"JA16EUCYEN","EUC-JP", //EUC 24-bit Japanese with '\' mapped to the Japanese yen character 
	"JA16MACSJIS","JA16MACSJIS", //Mac client Shift-JIS 16-bit Japanese  
	"JA16SJIS","JA16SJIS", //Shift-JIS 16-bit Japanese 
	"JA16SJISYEN","JA16SJISYEN", //Shift-JIS 16-bit Japanese with '\' mapped to the Japanese yen characters
	"JA16VMS","JA16VMS", //JVMS 16-bit Japanese 
	"KO16DBCS","KO16DBCS", //IBM EBCDIC 16-bit Korean 
	"KO16KSC5601","KO16KSC5601", //KSC5601 16-bit Korean 
	"KO16KSCCS","KO16KSCCS", //KSCCS 16-bit Korean 
	"KO16MSWIN949","KO16MSWIN949", //MS Windows Code Page 949 Korean 
	"LA8ISO6937","LA8ISO6937", //ISO 6937 8-bit Coded Character Set for Text Communication 
	"LA8PASSPORT","LA8PASSPORT", //German Government Printer 8-bit All-European Latin 
	"LV8PC8LR","LV8PC8LR", //Latvian Version IBM-PC Code Page 866 8-bit Latin/Cyrillic 
	"NDK7DEC","NDK7DEC", //DEC VT100 7-bit Norwegian/Danish 
	"NL7DEC","NL7DEC", //DEC VT100 7-bit Dutch 
	"S7DEC","S7DEC", //DEC VT100 7-bit Swedish 
	"S7SIEMENS9780X","S7SIEMENS9780X", //Siemens 97801/97808 7-bit Swedish 
	"S8BS2000","S8BS2000", //Siemens 9750-62 EBCDIC 8-bit Swedish 
	"S8EBCDIC1143","S8EBCDIC1143", //EBCDIC Code Page 1143 8-bit Swedish 
	"S8EBCDIC278","S8EBCDIC278", //EBCDIC Code Page 278/1 8-bit Swedish 
	"SF7DEC","SF7DEC", //DEC VT100 7-bit Finnish 
	"TH8MACTHAI","TH8MACTHAI", //Mac Client 8-bit Latin/Thai 
	"TH8MACTHAIS","TH8MACTHAIS", //Mac Server 8-bit Latin/Thai 
	"TH8TISASCII","TH8TISASCII", //Thai Industrial Standard 620-2533 - ASCII 8-bit 
	"TH8TISEBCDIC","TH8TISEBCDIC", //Thai Industrial Standard 620-2533 - EBCDIC 8-bit 
	"TH8TISEBCDICS","TH8TISEBCDICS", //Thai Industrial Standard 620-2533-EBCDIC Server 8-bit 
	"TR7DEC","TR7DEC", //DEC VT100 7-bit Turkish 
	"TR8DEC","TR8DEC", //DEC 8-bit Turkish 
	"TR8EBCDIC1026","TR8EBCDIC1026", //EBCDIC Code Page 1026 8-bit Turkish 
	"TR8EBCDIC1026S","TR8EBCDIC1026S", //EBCDIC Code Page 1026 Server 8-bit Turkish 
	"TR8MACTURKISH","TR8MACTURKISH", //Mac Client 8-bit Turkish 
	"TR8MACTURKISHS","TR8MACTURKISHS", //MAC Server 8-bit Turkish 
	"TR8MSWIN1254","TR8MSWIN1254", //MS Windows Code Page 1254 8-bit Turkish 
	"TR8PC857","TR8PC857", //IBM-PC Code Page 857 8-bit Turkish 
	"US8BS2000","US8BS2000", //Siemens 9750-62 EBCDIC 8-bit American 
	"US8PC437","US8PC437", //IBM-PC Code Page 437 8-bit American 
	"UTF8","UTF-8", 
	"UTFE","UTFE",
	"VN8MSWIN1258","VN8MSWIN1258", //MS Windows Code Page 1258 8-bit Vietnamese 
	"VN8VN3VN3","VN8VN3VN3", //8-bit Vietnamese
	"WE8BS2000L5","WE8BS2000L5", //Siemens EBCDIC.DF.L5 8-bit West European/Turkish 
	"WE8DEC","WE8DEC", //DEC 8-bit West European 
	"WE8DG","WE8DG", //DG 8-bit West European 
	"WE8EBCDIC1047","WE8EBCDIC1047", //EBCDIC Code Page 1047 8-bit West European 
	"WE8EBCDIC1047E","WE8EBCDIC1047E", //Latin 1/Open Systems 1047 
	"WE8EBCDIC1140","WE8EBCDIC1140", //EBCDIC Code Page 1140 8-bit West European 
	"WE8EBCDIC1140C","WE8EBCDIC1140C", //EBCDIC Code Page 1140 Client 8-bit West European 
	"WE8EBCDIC1145","WE8EBCDIC1145", //EBCDIC Code Page 1145 8-bit West European 
	"WE8EBCDIC1146","WE8EBCDIC1146", //EBCDIC Code Page 1146 8-bit West European 
	"WE8EBCDIC1148","WE8EBCDIC1148", //EBCDIC Code Page 1148 8-bit West European 
	"WE8EBCDIC1148C","WE8EBCDIC1148C", //EBCDIC Code Page 1148 Client 8-bit West European 
	"WE8EBCDIC284","WE8EBCDIC284", //EBCDIC Code Page 284 8-bit Latin American/Spanish 
	"WE8EBCDIC285","WE8EBCDIC285", //EBCDIC Code Page 285 8-bit West European 
	"WE8EBCDIC37","WE8EBCDIC37", //EBCDIC Code Page 37 8-bit West European 
	"WE8EBCDIC37C","WE8EBCDIC37C", //EBCDIC Code Page 37 8-bit Oracle/c 
	"WE8EBCDIC500","WE8EBCDIC500", //EBCDIC Code Page 500 8-bit West European 
	"WE8EBCDIC500C","WE8EBCDIC500C", //EBCDIC Code Page 500 8-bit Oracle/c 
	"WE8EBCDIC871","WE8EBCDIC871", //EBCDIC Code Page 871 8-bit Icelandic 
	"WE8EBCDIC924","WE8EBCDIC924", //Latin 9 EBCDIC 924 
	"WE8HP","WE8HP", //HP LaserJet 8-bit West European 
	"WE8ISO8859P9","ISO-8859-9", //ISO 8859-9 West European & Turkish 
	"WE8MSWIN1252","WE8MSWIN1252", //MS Windows Code Page 1252 8-bit West European 
	"WE8NCR4970","WE8NCR4970", //NCR 4970 8-bit West European 
	"WE8NEXTSTEP","WE8NEXTSTEP", //NeXTSTEP PostScript 8-bit West European 
	"WE8ROMAN8","WE8ROMAN8", //HP Roman8 8-bit West European 
	"ZHS16CGB231280","ZHS16CGB231280", //CGB2312-80 16-bit Simplified Chinese 
	"ZHS16DBCS","ZHS16DBCS", //IBM EBCDIC 16-bit Simplified Chinese 
	"ZHS16GBK", "ZHS16GBK", //GBK 16-bit Simplified Chinese 
	"ZHS16MACCGB231280","ZHS16MACCGB231280", //Mac client CGB2312-80 16-bit Simplified Chinese 
	"ZHS32GB18030","ZHS32GB18030", //GB18030-2000 
	"ZHT16BIG5","ZHT16BIG5", //BIG5 16-bit Traditional Chinese 
	"ZHT16CCDC","ZHT16CCDC", //HP CCDC 16-bit Traditional Chinese 
	"ZHT16DBCS","ZHT16DBCS", //IBM EBCDIC 16-bit Traditional Chinese 
	"ZHT16DBT","ZHT16DBT", //Taiwan Taxation 16-bit Traditional Chinese 
	"ZHT16HKSCS","ZHT16HKSCS", //MS Windows Code Page 950 with Hong Kong Supplementary Character Set 
	"ZHT16MSWIN950","ZHT16MSWIN950", //MS Windows Code Page 950 Traditional Chinese 
	"ZHT32EUC","ZHT32EUC", //EUC 32-bit Traditional Chinese 
	"ZHT32SOPS","ZHT32SOPS", // SOPS 32-bit Traditional Chinese 
	"ZHT32TRIS","ZHT32TRIS", //TRIS 32-bit Traditional Chinese 
	""
};

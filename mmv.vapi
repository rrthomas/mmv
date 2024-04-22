[CCode (cprefix = "", lower_case_cprefix = "", cheader_filename = "mmv.h")]
namespace Mmv {
	[CCode (cname = "REP")]
	public struct Rep {
		// HANDLE *r_hfrom;
		// FILEINFO *r_ffrom;
		// HANDLE *r_hto;
		// char *r_nto;			/* non-path part of new name */
		// FILEINFO *r_fdel;
		// struct rep *r_first;
		// struct rep *r_thendo;
		// struct rep *r_next;
		// int r_flags;
	}

	public delegate int ScanCallback(Rep r);

	public int skipdel(Rep r);
	public int baddel(Rep r);

	public int dostage(string lastend, char *pathend, [CCode (array_length = false)] string[] start1, [CCode (array_length = false)] long[] len, int stage, int anylev);
	public int parsepat();
	public void checkcollisions();
	public void findorder();
	public void nochains();
	public void scandeletes(ScanCallback cb);
	public void goonordie();
	public void doreps();
}

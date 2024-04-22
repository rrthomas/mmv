typedef struct {
        char *fi_name;
        struct rep *fi_rep;
        mode_t fi_mode;
        int fi_stflags;
} FILEINFO;

typedef struct {
        dev_t di_vid;
        ino_t di_did;
        size_t di_nfils;
        FILEINFO **di_fils;
        char di_flags;
        const char *di_path; /* Only set when DI_NONEXISTENT is set */
} DIRINFO;

typedef struct {
        char *h_name;
        DIRINFO *h_di;
        char h_err;
} HANDLE;

typedef struct rep {
        HANDLE *r_hfrom;
        FILEINFO *r_ffrom;
        HANDLE *r_hto;
        char *r_nto;			/* non-path part of new name */
        FILEINFO *r_fdel;
        struct rep *r_first;
        struct rep *r_thendo;
        struct rep *r_next;
        int r_flags;
} REP;

typedef struct {
        REP *rd_p;
        DIRINFO *rd_dto;
        char *rd_nto;
        unsigned rd_i;
} REPDICT;

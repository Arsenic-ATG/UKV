package ukv

/*
#cgo LDFLAGS: -L${SRCDIR}/../../lib -lukv_stl -lstdc++
#cgo CFLAGS: -g -Wall -I${SRCDIR}/../../include

#include "ukv/db.h"
#include <stdlib.h>
*/
import "C"
import (
	u "github.com/unum-cloud/UKV/golang/internal"
)

type STL struct {
	u.DataBase
}

func CreateDB() STL {
	backend := u.BackendInterface{
		UKV_error_free:      C.ukv_error_free,
		UKV_arena_free:      C.ukv_arena_free,
		UKV_open:            C.ukv_db_open,
		UKV_free:            C.ukv_db_free,
		UKV_read:            C.ukv_read,
		UKV_write:           C.ukv_write,
		UKV_val_len_missing: u.UKV_val_len_t(C.ukv_val_len_missing_k)}

	db := STL{DataBase: u.DataBase{Backend: backend}}
	return db
}

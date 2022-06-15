import threading
import ukv


def only_explicit(col):

    col.set(3, 'x'.encode())
    col.set(4, 'y'.encode())
    assert col.get(3) == 'x'.encode()
    assert col.get(4) == 'y'.encode()


def only_operators(col):

    assert 1 not in col
    assert 2 not in col

    col[1] = 'a'.encode()
    col[2] = 'bb'.encode()
    assert 1 in col
    assert 2 in col
    assert col[1] == 'a'.encode()
    assert col[2] == 'bb'.encode()

    del col[1]
    del col[2]
    assert 1 not in col
    assert 2 not in col


def test_main_collection():
    db = ukv.DataBase()
    only_explicit(db)
    only_operators(db)


def test_named_collections():
    db = ukv.DataBase()
    col_sub = db['sub']
    col_dub = db['dub']
    only_explicit(col_sub)
    only_explicit(col_dub)
    only_operators(col_sub)
    only_operators(col_dub)


def test_main_collection_txn():

    with ukv.DataBase() as db:
        with ukv.Transaction(db) as txn:
            only_explicit(txn)
            only_operators(txn)


def test_multithreaded_transaction():
    keys_count = 100

    def set_same_value(txn, value):
        for index in range(keys_count):
            txn.set(index, value.encode())
        for index in range(keys_count):
            assert txn.get(index) == value.encode()

    with ukv.DataBase() as db:
        with ukv.Transaction(db) as txn:
            thread_count = 128
            threads = [None] * thread_count
            for thread_idx in range(thread_count):
                threads[thread_idx] = threading.Thread(
                    target=set_same_value, args=(txn, str(thread_idx,)))
            for thread_idx in range(thread_count):
                threads[thread_idx].start()
            for thread_idx in range(thread_count):
                threads[thread_idx].join()

            for index in range(keys_count-1):
                assert txn.get(index) == txn.get(index+1)

#! /bin/awk -f
BEGIN {
        WORDNUM = 1000
        for (i = 1; i <= WORDNUM; i++) {
                printf("%d %s\n", randint(WORDNUM), randword(20))
        }
}

# randint(n): return a random integer number which is >= 1 and <= n
function randint(n) {
        return int(n *rand()) + 1
}

# randlet(): return a random letter, which maybe upper, lower or number. 
function randlet() {
        return substr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", randint(62), 1)
}

# randword(LEN): return a rand word with a length of LEN
function randword(LEN) {
        randw=""
        for( j = 1; j <= LEN; j++) {
                randw=randw randlet()
        }
        return randw
}

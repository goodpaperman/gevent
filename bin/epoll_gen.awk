#! /bin/awk -f
# run following command:
#   awk -f epoll_gen.awk > demo
# instead of sh epoll_gen.awk
# on raspberry, otherwise you will got following errors:
#
# epoll_gen.awk: 7: epoll_gen.awk: BEGIN: not found
# epoll_gen.awk: 8: epoll_gen.awk: WORDNUM: not found
# epoll_gen.awk: 9: epoll_gen.awk: Syntax error: Bad for loop variable
#
# don't know why, amazing...


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

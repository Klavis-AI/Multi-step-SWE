def outer(&) = inner(&)
def inner = yield
p outer { 7 }

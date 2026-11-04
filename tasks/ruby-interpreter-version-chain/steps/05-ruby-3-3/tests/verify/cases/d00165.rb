def outer(*) = inner(*)
def inner(a, b) = [a, b]
p outer(1, 2)

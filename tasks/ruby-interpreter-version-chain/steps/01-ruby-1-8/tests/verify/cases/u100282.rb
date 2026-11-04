struct_test = Struct.new("Test", :foo, :bar)
    p(struct_test)

    test = struct_test.new(1, 2)
    p(test.foo)
    p(test.bar)
    p(test[0])
    p(test[1])

    a, b = test.to_a
    p(a)
    p(b)

    test[0] = 22
    p(test.foo)

    test.bar = 47
    p(test.bar)
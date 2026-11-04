s = "aBc"
    s.upcase
    p(s)
    s.upcase!
    p(s)

    s = "aBc"
    s.downcase
    p(s)
    s.downcase!
    p(s)

    s = "aBc"
    s.swapcase
    p(s)
    s.swapcase!
    p(s)

    s = "aBc"
    s.capitalize
    p(s)
    s.capitalize!
    p(s)

    s = "aBc"
    s.tr("a-z", "A-Z")
    p(s)
    s.tr!("a-z", "A-Z")
    p(s)

    s = "aaBBcc"
    s.tr_s("a-z", "A-Z")
    p(s)
    s.tr_s!("a-z", "A-Z")
    p(s)

    s = "aaBBcc"
    s.squeeze("a-z")
    p(s)
    s.squeeze!("a-z")
    p(s)

    s = "aaBBcc"
    s.delete("a-z")
    p(s)
    s.delete!("a-z")
    p(s)
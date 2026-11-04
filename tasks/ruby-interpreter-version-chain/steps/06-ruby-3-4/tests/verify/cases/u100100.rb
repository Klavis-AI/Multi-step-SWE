def aaa(a, b=100, *rest)
    res = [a, b]
    res += rest if rest
    return res
  end
begin; aaa()
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    begin; aaa
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(aaa(1))
    p(aaa(1, 2))
    p(aaa(1, 2, 3, 4))
    p(aaa(1, *[2, 3, 4]))
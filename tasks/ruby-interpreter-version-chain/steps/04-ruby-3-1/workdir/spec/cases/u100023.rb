@cls = Array
a = @cls[*(1..100).to_a]

    p(a[0])
    p(a[99])
    p(a[100])
    p(a[-1])
    p(a[-2])
    p(a[-100])
    p(a[-101])
    p(a[-101,0])
    p(a[-101,1])
    p(a[-101,-1])
    p(a[10,-1])

    p(a[0,1])
    p(a[99,1])
    p(a[100,1])
    p(a[99,100])
    p(a[-1,1])
    p(a[-2,1])
    p(a[-100,0])
    p(a[-100,1])

    p(a[9, 3])
    p(a[-91, 3])

    p(a[0..0])
    p(a[99..99])
    p(a[100..100])
    p(a[99..200])
    p(a[-1..-1])
    p(a[-2..-2])

    p(a[9..11])
    p(a[-91..-89])
    
    p(a[10, -3])
    # Ruby 1.8 feature change:
    # Array#[size..x] returns [] instead of nil.
    #assert_nil(a[10..7])
    p(a[10..7])

    begin; a['cat']
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
def foo
    assert_equal(nil, ->(&b){ b }.call)
  end
f = ->(a,b,c=3,*d,&e){ [a,b,c,d,e.call(d + [a,b,c])] }
    p(f.call(1,2){|z| z.inject{|s,x| s+x} })
    p(->(&b){ b }.call)
    foo { puts "bogus block " }
    p(->(&b){ b.call }.call { 1 })
    b = nil
    p(->(&b){ b.call }.call { 1 })
    p(b)
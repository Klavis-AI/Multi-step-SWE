@cls = Array
a = @cls[ 1, 'cat', 1..1 ]
    p(a.map! {|e| e.class})
    p(a)
   
    a = @cls[ 1, 'cat', 1..1 ]
    p(a.map! { 99 })
    p(a)

    a = @cls[ ]
    p(a.map! { 99 })
    p(a)
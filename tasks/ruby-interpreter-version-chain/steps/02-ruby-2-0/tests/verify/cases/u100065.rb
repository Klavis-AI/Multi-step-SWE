@cls = Array
a = @cls[*%w( dog cat bee ant )]
    i = a.length
    a.reverse_each { |e|
      i -= 1
      p(e)
    }
    p(i)

    a = @cls[]
    i = 0
    a.reverse_each { |e|
      p(false)
    }
    p(i)
Bug4438 = '[ruby-core:35364]'
bug3237 = '[ruby-core:29948]'
    str = "\u2600"
    id = :"\u2604"
    e = begin; str.__send__(id)
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    p(e.message)
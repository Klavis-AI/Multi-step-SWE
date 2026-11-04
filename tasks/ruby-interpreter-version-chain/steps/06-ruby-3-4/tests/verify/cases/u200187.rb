case 5
    when 1, 2, 3, 4, 6, 7, 8
      p(false)
    when 5
      p(true)
    end

    case 5
    when 5
      p(true)
    when 1..10
      p(false)
    end

    case 5
    when 1..10
      p(true)
    else
      p(false)
    end

    case 5
    when 5
      p(true)
    else
      p(false)
    end

    case "foobar"
    when /^f.*r$/
      p(true)
    else
      p(false)
    end

    case
    when true
      p(true)
    when false, nil
      p(false)
    else
      p(false)
    end

    case "+"
    when *%w/. +/
      p(true)
    else
      p(false)
    end

    case
    when *[], false
      p(false)
    else
      p(true)
    end

    case
    when *false, []
      p(true)
    else
      p(false)
    end

    begin; case
      when false, *x, false
      end
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
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
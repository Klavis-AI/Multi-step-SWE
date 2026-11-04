@cls = Array
p(@cls[].length)
    p(@cls[1].length)
    p(@cls[1, nil].length)
    p(@cls[nil, 1].length)
    p(@cls[*(0..233).to_a].length)
begin
  case [1, 2, 3, 4, 5]
  in [*pre, 3 => hit, *post]
    p [pre, hit, post]
  end
rescue Exception => e
  p e.class
end

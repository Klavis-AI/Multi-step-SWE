begin
  case [1, 2, 3, 4]
  in [first, *rest] then p [first, rest]
  end
rescue Exception => e
  p e.class
end

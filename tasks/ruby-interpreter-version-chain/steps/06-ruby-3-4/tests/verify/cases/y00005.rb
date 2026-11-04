begin
  case {a: 1, b: 2, c: 3}
  in {a: 1, **rest} then p rest
  end
rescue Exception => e
  p e.class
end

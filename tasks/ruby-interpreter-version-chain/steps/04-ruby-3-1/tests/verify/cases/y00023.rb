begin
  p [1, 2, 3].map { _1 * 3 }
  p [[1, 2]].map { _1 + _2 }
rescue Exception => e
  p e.class
end

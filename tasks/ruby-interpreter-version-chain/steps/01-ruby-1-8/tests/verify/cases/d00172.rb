begin
  p([1, nil, 2].nitems)
rescue Exception => e
  p e.class
end

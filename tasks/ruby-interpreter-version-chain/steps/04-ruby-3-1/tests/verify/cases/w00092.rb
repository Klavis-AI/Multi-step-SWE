begin
  p([1, 2, 3].zip([9]))
rescue Exception => e
  p e.class
end

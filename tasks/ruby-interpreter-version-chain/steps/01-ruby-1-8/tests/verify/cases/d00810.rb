begin
  e = [1, 2, 3].each
  p e.next
  p e.next
rescue Exception => ex
  p ex.class
end

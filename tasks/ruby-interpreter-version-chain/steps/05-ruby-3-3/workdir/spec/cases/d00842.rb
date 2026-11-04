begin
  "a".taint
  p :done
rescue Exception => e
  p e.class
end

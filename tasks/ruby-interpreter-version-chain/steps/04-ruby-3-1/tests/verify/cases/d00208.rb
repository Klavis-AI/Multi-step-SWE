begin
  p({a: 1}.except(:a))
rescue Exception => e
  p e.class
end

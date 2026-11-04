begin
  p "pre_x".delete_prefix("pre_")
rescue Exception => e
  p e.class
end

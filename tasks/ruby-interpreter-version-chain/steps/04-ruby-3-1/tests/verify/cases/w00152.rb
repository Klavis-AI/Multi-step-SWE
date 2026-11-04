begin
  p("abc".gsub(/[ab]/) { |m| m.upcase })
rescue Exception => e
  p e.class
end

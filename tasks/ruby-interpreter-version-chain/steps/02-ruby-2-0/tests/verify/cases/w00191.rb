begin
  p("a\tb".gsub(/[ab]/) { |m| m.upcase })
rescue Exception => e
  p e.class
end

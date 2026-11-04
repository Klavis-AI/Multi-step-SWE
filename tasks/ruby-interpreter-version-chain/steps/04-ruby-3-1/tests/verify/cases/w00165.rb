begin
  p("日本語".gsub(/[ab]/) { |m| m.upcase })
rescue Exception => e
  p e.class
end

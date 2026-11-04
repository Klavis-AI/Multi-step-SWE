# Be sure to restart your server when you modify this file.

# Configure sensitive parameters which will be filtered from the log file.
#
# `filter_parameters` matches a key when the key CONTAINS the entry, case
# insensitively (ActiveSupport::ParameterFilter), and it descends into nested
# hashes.  That is what makes three entries enough for this application:
#
#   :From, :To    every Twilio webhook posts the caller's number as `From` and
#                 the line they dialled as `To`.  Both are E.164 telephone
#                 numbers belonging to a member of the public who rang a
#                 survey line, and `ActionController`'s own request logger
#                 writes the whole parameter hash at :info, which is the level
#                 production runs at.  Substring matching also covers `token`,
#                 `auth_token` and `authenticity_token`, which is not why they
#                 are here but is not unwelcome either.
#   :email        the neighbourhood-subscription form posts it nested, as
#                 `subscription[email]`; the filter descends, so naming the
#                 leaf is enough and `subscription` itself stays readable.
#
# `:password` was here before any of this and stays: the application has no
# password field today, and an application that grows one should not have to
# remember.
Rails.application.config.filter_parameters += [:password, :From, :To, :email]

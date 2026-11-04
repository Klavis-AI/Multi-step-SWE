package auth

import (
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/codedellemc/libstorage/api/context"
	"github.com/codedellemc/libstorage/api/types"
)

const (
	jwtAlg       = "HS256"
	jwtKey       = "key"
	jwtAkutz     = `eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjQxMDI0NDQ4MDAsImlhdCI6MTQ5MTIzODk1MCwibmJmIjoxNDkxMjM4OTUwLCJzdWIiOiJha3V0eiJ9.BNB4Yiq2AwwPPeUTbCwvAcoLe-L-O8WGQ4osc5D3Ijg`
	jwtCduchesne = `eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjQxMDI0NDQ4MDAsImlhdCI6MTQ5MTI0Mzk4NCwibmJmIjoxNDkxMjQzOTg0LCJzdWIiOiJjZHVjaGVzbmUifQ.Ol0I6yW2aYUJoSXjHSYoqt6gzZ3hYzf5tNcwjBMk8ws`
)

func TestValidateAuthToken_ValidTokSigKey(t *testing.T) {
	sc := &types.AuthConfig{
		Key:   []byte(jwtKey),
		Alg:   jwtAlg,
		Allow: []string{"akutz"},
	}
	tok, err := ValidateAuthTokenWithJWT(context.Background(), sc, jwtAkutz)
	if !assert.NoError(t, err) {
		t.FailNow()
	}
	if !assert.NotNil(t, tok) {
		t.FailNow()
	}
	if !assert.Equal(t, "akutz", tok.Subject) {
		t.FailNow()
	}
}

func TestValidateAuthToken_NotInAllowList(t *testing.T) {
	sc := &types.AuthConfig{
		Key: []byte(jwtKey),
		Alg: jwtAlg,
	}
	tok, err := ValidateAuthTokenWithJWT(context.Background(), sc, jwtAkutz)
	if !assert.Error(t, err) {
		t.FailNow()
	}
	if !assert.Nil(t, tok) {
		t.FailNow()
	}
	if !assert.IsType(t, &types.ErrSecTokInvalid{}, err) {
		t.FailNow()
	}
	terr := err.(*types.ErrSecTokInvalid)
	if !assert.True(t, terr.Denied) {
		t.FailNow()
	}
}

func TestValidateAuthToken_InDenyList(t *testing.T) {
	sc := &types.AuthConfig{
		Key:  []byte(jwtKey),
		Alg:  jwtAlg,
		Deny: []string{"akutz"},
	}
	tok, err := ValidateAuthTokenWithJWT(context.Background(), sc, jwtAkutz)
	if !assert.Error(t, err) {
		t.FailNow()
	}
	if !assert.Nil(t, tok) {
		t.FailNow()
	}
	if !assert.IsType(t, &types.ErrSecTokInvalid{}, err) {
		t.FailNow()
	}
	terr := err.(*types.ErrSecTokInvalid)
	if !assert.True(t, terr.Denied) {
		t.FailNow()
	}
}

func TestValidateAuthToken_InvalidKey(t *testing.T) {
	sc := &types.AuthConfig{
		Key: []byte("invalidKey"),
		Alg: jwtAlg,
	}
	tok, err := ValidateAuthTokenWithJWT(context.Background(), sc, jwtAkutz)
	if !assert.Error(t, err) {
		t.FailNow()
	}
	if !assert.Nil(t, tok) {
		t.FailNow()
	}
	if !assert.IsType(t, &types.ErrSecTokInvalid{}, err) {
		t.FailNow()
	}
	terr := err.(*types.ErrSecTokInvalid)
	if !assert.True(t, terr.InvalidSig) {
		t.FailNow()
	}
}

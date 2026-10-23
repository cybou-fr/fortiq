package fr.cybou.fortiq;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/**
 * Remembered SSH secrets (password / key passphrase). One non-exportable AES-256-GCM key lives in
 * the Android Keystore; callers keep only the ciphertext. The server id is bound as additional
 * authenticated data, so a blob cannot be moved to another server entry. Methods never throw and
 * never put secret material in lastError(): they return null and describe the failure.
 */
public final class SecretVault {
    private static final String PROVIDER = "AndroidKeyStore";
    private static final String ALIAS = "fortiq.ssh.secrets.v1";
    private static final int IV_BYTES = 12;
    private static final int TAG_BITS = 128;
    private static volatile String lastError = "";

    private SecretVault() {}

    public static String lastError() { return lastError; }

    private static synchronized SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance(PROVIDER);
        store.load(null);
        if (!store.containsAlias(ALIAS)) {
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, PROVIDER);
            generator.init(new KeyGenParameterSpec.Builder(ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .build());
            generator.generateKey();
        }
        return (SecretKey) store.getKey(ALIAS, null);
    }

    /** Returns iv || ciphertext, or null (see lastError()). */
    public static byte[] encrypt(byte[] aad, byte[] plain) {
        try {
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key());
            cipher.updateAAD(aad);
            byte[] iv = cipher.getIV();
            byte[] ct = cipher.doFinal(plain);
            byte[] out = new byte[iv.length + ct.length];
            System.arraycopy(iv, 0, out, 0, iv.length);
            System.arraycopy(ct, 0, out, iv.length, ct.length);
            lastError = "";
            return out;
        } catch (Throwable t) {
            lastError = "Android Keystore: " + t.getClass().getSimpleName();
            return null;
        }
    }

    /** Returns the plaintext, or null (see lastError()); a tampered or foreign blob fails authentication. */
    public static byte[] decrypt(byte[] aad, byte[] blob) {
        try {
            if (blob == null || blob.length <= IV_BYTES) { lastError = "stored secret is damaged"; return null; }
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(TAG_BITS, blob, 0, IV_BYTES));
            cipher.updateAAD(aad);
            byte[] plain = cipher.doFinal(blob, IV_BYTES, blob.length - IV_BYTES);
            lastError = "";
            return plain;
        } catch (Throwable t) {
            lastError = "Android Keystore: " + t.getClass().getSimpleName();
            return null;
        }
    }
}

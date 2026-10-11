#include "flutter_window.h"

#include <optional>
#include <vector>
#include <wincrypt.h>

#include <flutter/standard_method_codec.h>

#include "flutter/generated_plugin_registrant.h"

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());

  // Seal license keys using the current Windows user's DPAPI credentials.
  // Only ciphertext is persisted by Dart; secrets never enter preferences.
  protected_storage_channel_ =
      std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
          flutter_controller_->engine()->messenger(),
          "fortiq/protected_storage",
          &flutter::StandardMethodCodec::GetInstance());
  protected_storage_channel_->SetMethodCallHandler(
      [](const flutter::MethodCall<flutter::EncodableValue>& call,
         std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
        if (call.method_name() != "protect" &&
            call.method_name() != "unprotect") {
          result->NotImplemented();
          return;
        }
        const auto* input = call.arguments()
            ? std::get_if<std::vector<uint8_t>>(call.arguments())
            : nullptr;
        if (!input || input->empty()) {
          result->Error("invalid_input", "Expected non-empty binary data.");
          return;
        }
        DATA_BLOB original = {
            static_cast<DWORD>(input->size()),
            const_cast<BYTE*>(input->data()),
        };
        DATA_BLOB output = {};
        BOOL ok = call.method_name() == "protect"
            ? ::CryptProtectData(&original, L"FORTIQ license", nullptr, nullptr,
                                 nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
            : ::CryptUnprotectData(&original, nullptr, nullptr, nullptr,
                                   nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
        if (!ok) {
          result->Error("dpapi_failed", "Windows could not protect the license.");
          return;
        }
        std::vector<uint8_t> bytes(output.pbData,
                                   output.pbData + output.cbData);
        ::LocalFree(output.pbData);
        result->Success(flutter::EncodableValue(bytes));
      });
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  if (flutter_controller_) {
    protected_storage_channel_.reset();
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}

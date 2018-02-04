#include "pch.h"

//
union bgra8_t {
	// v is ordered with blue being the least and alpha the most significant octett.
	uint32_t v;
	struct {
		uint8_t b;
		uint8_t g;
		uint8_t r;
		uint8_t a;
	} c;
};

struct App : winrt::Windows::UI::Xaml::ApplicationT<App> {
	void OnLaunched(const winrt::Windows::ApplicationModel::Activation::LaunchActivatedEventArgs&) {
		m_root.HorizontalScrollBarVisibility(winrt::Windows::UI::Xaml::Controls::ScrollBarVisibility::Auto);
		m_root.VerticalScrollBarVisibility(winrt::Windows::UI::Xaml::Controls::ScrollBarVisibility::Auto);
		m_root.ZoomMode(winrt::Windows::UI::Xaml::Controls::ZoomMode::Enabled);
		m_root.MinZoomFactor(0.1f);
		m_root.MaxZoomFactor(2.0f);

		{
			const winrt::Windows::UI::Xaml::Controls::MenuFlyout flyout;

			const auto flyout_append = [=](winrt::hstring text) -> winrt::Windows::UI::Xaml::Controls::MenuFlyoutItem {
				winrt::Windows::UI::Xaml::Controls::MenuFlyoutItem item;
				item.Text(text);

				flyout.Items().Append(item);

				return item;
			};

			flyout_append(L"load").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->load_file();
			});
			flyout_append(L"load overlay").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->load_overlay();
			});
			flyout_append(L"save as jpg").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->save_jpg();
			});
			flyout_append(L"histogram").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->do_histogram();
			});
			flyout_append(L"red").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->do_red();
			});
			flyout_append(L"green").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->do_green();
			});
			flyout_append(L"blue").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->do_blue();
			});
			flyout_append(L"grayscale").Click([this](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
				this->do_grayscale();
			});

			m_root.ContextFlyout(flyout);
		}

		const auto window = winrt::Windows::UI::Xaml::Window::Current();
		window.Content(m_root);
		window.Activate();
	}

private:
	winrt::fire_and_forget load_file() {
		const winrt::Windows::Storage::Pickers::FileOpenPicker picker;
		picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::PicturesLibrary);
		picker.FileTypeFilter().ReplaceAll({
			L".bmp",
			L".gif",
			L".ico",
			L".jpeg",
			L".jpg",
			L".png",
			L".tiff",
		});

		const auto file = co_await picker.PickSingleFileAsync();
		if (!file) {
			return;
		}

		const auto bitmap = co_await writeable_bitmap_from_file(file);
		refresh_image(bitmap);
		m_bitmap = bitmap;
	}

	winrt::fire_and_forget load_overlay() {
		const winrt::Windows::Storage::Pickers::FileOpenPicker picker;
		picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::PicturesLibrary);
		picker.FileTypeFilter().ReplaceAll({
			L".bmp",
			L".gif",
			L".ico",
			L".jpeg",
			L".jpg",
			L".png",
			L".tiff",
		});

		const auto file = co_await picker.PickSingleFileAsync();
		if (!file) {
			return;
		}

		const auto buffer = get_buffer_from_writeable_bitmap(m_bitmap);

		const auto overlay = co_await writeable_bitmap_from_file(file);
		const auto overlay_buffer = get_buffer_from_writeable_bitmap(overlay);

		const auto stride = m_bitmap.PixelWidth();
		const auto overlay_stride = overlay.PixelWidth();
		const auto width = std::min(stride, overlay_stride);
		const auto height = std::min(m_bitmap.PixelHeight(), overlay.PixelHeight());

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				// Porter-Duff Source Over Destination rule
				// Cr = Cs + Cd*(1-As)
				// Ar = As + Ad*(1-As)
				const auto d = buffer[y * stride + x];
				const auto s = overlay_buffer[y * overlay_stride + x];
				bgra8_t r;
				r.c.b = uint8_t(std::min(255, (s.c.b * s.c.a + d.c.b * (255 - s.c.a)) / 255));
				r.c.g = uint8_t(std::min(255, (s.c.g * s.c.a + d.c.g * (255 - s.c.a)) / 255));
				r.c.r = uint8_t(std::min(255, (s.c.r * s.c.a + d.c.r * (255 - s.c.a)) / 255));
				r.c.a = uint8_t(std::min(255, (s.c.a * s.c.a + d.c.a * (255 - s.c.a)) / 255));
				buffer[y * stride + x] = r;
			}
		}

		m_bitmap.Invalidate();
	}

	winrt::fire_and_forget save_jpg() {
		if (!m_bitmap) {
			return;
		}

		const winrt::Windows::Storage::Pickers::FileSavePicker picker;
		picker.SuggestedStartLocation(winrt::Windows::Storage::Pickers::PickerLocationId::PicturesLibrary);
		picker.FileTypeChoices().Insert(L"JPEG files", winrt::single_threaded_vector<winrt::hstring>({ L".jpg" }));

		const auto file = co_await picker.PickSaveFileAsync();
		if (!file) {
			return;
		}

		const auto stream = co_await file.OpenAsync(winrt::Windows::Storage::FileAccessMode::ReadWrite);

		const winrt::Windows::UI::Xaml::Controls::Slider quality_slider;
		quality_slider.Minimum(0);
		quality_slider.Maximum(100);
		quality_slider.Value(80);
		quality_slider.StepFrequency(1);

		const winrt::Windows::UI::Xaml::Controls::ContentDialog quality_dialog;
		quality_dialog.Title(winrt::Windows::Foundation::PropertyValue::CreateString(L"JPEG Quality"));
		quality_dialog.Content(quality_slider);
		quality_dialog.PrimaryButtonText(L"Save");
		quality_dialog.SecondaryButtonText(L"Cancel");
		const auto quality_dialog_result = co_await quality_dialog.ShowAsync();

		if (quality_dialog_result != winrt::Windows::UI::Xaml::Controls::ContentDialogResult::Primary) {
			return;
		}

		const auto quality = quality_slider.Value() / 100.0;

		const auto encoder_options = winrt::Windows::Graphics::Imaging::BitmapPropertySet();
		encoder_options.Insert(L"ImageQuality", winrt::Windows::Graphics::Imaging::BitmapTypedValue(winrt::Windows::Foundation::PropertyValue::CreateDouble(quality), winrt::Windows::Foundation::PropertyType::Single));

		const auto encoder = co_await winrt::Windows::Graphics::Imaging::BitmapEncoder::CreateAsync(winrt::Windows::Graphics::Imaging::BitmapEncoder::JpegEncoderId(), stream, encoder_options);
		encoder.SetSoftwareBitmap(software_bitmap_from_writable_bitmap(m_bitmap));
		encoder.IsThumbnailGenerated(false);
		co_await encoder.FlushAsync();
	}

	void do_histogram() {
		if (!m_bitmap) {
			return;
		}

		const auto buffer = get_buffer_from_writeable_bitmap(m_bitmap);
		std::array<uint_fast64_t, 256> histogram{ 0 };

		for (auto& p : buffer) {
			auto c = (p.c.r + p.c.g + p.c.b) / uint8_t(3);
			++histogram[c];
		}

		const auto max = *std::max_element(histogram.begin(), histogram.end());

		for (auto& h : histogram) {
			h = 256 - (h * 256) / max;
		}

		const auto stride = m_bitmap.PixelWidth();

		for (int x = 0; x < 256; ++x) {
			auto h = histogram[x];

			for (uint_fast64_t y = h; y < 256; ++y) {
				auto index = stride * y + x;
				buffer[index].v = y == h ? 0xff000000 : 0xffffffff;
			}
		}

		m_bitmap.Invalidate();
	}

	void do_grayscale() {
		if (!m_bitmap) {
			return;
		}

		const auto buffer = get_buffer_from_writeable_bitmap(m_bitmap);

		for (auto& p : buffer) {
			p.c.r = p.c.g = p.c.b = (p.c.r + p.c.g + p.c.b) / uint8_t(3);
		}

		m_bitmap.Invalidate();
	}

	void do_red() {
		if (!m_bitmap) {
			return;
		}

		const auto buffer = get_buffer_from_writeable_bitmap(m_bitmap);

		for (auto& p : buffer) {
			p.v &= 0xffff0000;
		}

		m_bitmap.Invalidate();
	}

	void do_green() {
		if (!m_bitmap) {
			return;
		}

		const auto buffer = get_buffer_from_writeable_bitmap(m_bitmap);

		for (auto& p : buffer) {
			p.v &= 0xff00ff00;
		}

		m_bitmap.Invalidate();
	}

	void do_blue() {
		if (!m_bitmap) {
			return;
		}

		const auto buffer = get_buffer_from_writeable_bitmap(m_bitmap);

		for (auto& p : buffer) {
			p.v &= 0xff0000ff;
		}

		m_bitmap.Invalidate();
	}

	void refresh_image(const winrt::Windows::UI::Xaml::Media::ImageSource& source) {
		const winrt::Windows::UI::Xaml::Controls::Image image;
		image.HorizontalAlignment(winrt::Windows::UI::Xaml::HorizontalAlignment::Center);
		image.VerticalAlignment(winrt::Windows::UI::Xaml::VerticalAlignment::Center);
		image.Source(source);

		const winrt::event_token token = image.Loaded([=](const winrt::Windows::Foundation::IInspectable&, const winrt::Windows::UI::Xaml::RoutedEventArgs&) {
			image.Loaded(token);

			auto scale_x = m_root.ViewportWidth() / image.ActualWidth();
			auto scale_y = m_root.ViewportHeight() / image.ActualHeight();
			auto scale = float(std::min(scale_x, scale_y));

			if (scale < 1) {
				m_root.ChangeView({}, {}, scale);
			}
		});

		m_root.Content(image);
	}

	static winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::UI::Xaml::Media::Imaging::WriteableBitmap> writeable_bitmap_from_file(const winrt::Windows::Storage::StorageFile& file) {
		const auto stream = co_await file.OpenReadAsync();
		const auto decoder = co_await winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream);

		const auto transform = winrt::Windows::Graphics::Imaging::BitmapTransform();
		const auto pixel_data_provider = co_await decoder.GetPixelDataAsync(
			winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
			winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Straight,
			transform,
			winrt::Windows::Graphics::Imaging::ExifOrientationMode::RespectExifOrientation,
			winrt::Windows::Graphics::Imaging::ColorManagementMode::ColorManageToSRgb
		);

		auto pixel_data = pixel_data_provider.DetachPixelData();

		const winrt::Windows::UI::Xaml::Media::Imaging::WriteableBitmap bitmap(decoder.OrientedPixelWidth(), decoder.OrientedPixelHeight());
		span_memcpy(get_buffer_from_writeable_bitmap(bitmap), gsl::span<byte>(pixel_data));
		return bitmap;
	}

	static winrt::Windows::Graphics::Imaging::SoftwareBitmap software_bitmap_from_writable_bitmap(const winrt::Windows::UI::Xaml::Media::Imaging::WriteableBitmap& bitmap) {
		return winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(bitmap.PixelBuffer(), winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, bitmap.PixelWidth(), bitmap.PixelHeight());
	}

	static gsl::span<bgra8_t> get_buffer_from_writeable_bitmap(const winrt::Windows::UI::Xaml::Media::Imaging::WriteableBitmap& bitmap) {
		const auto pixel_buffer = bitmap.PixelBuffer();
		const auto buffer = pixel_buffer.as<Windows::Storage::Streams::IBufferByteAccess>();

		const auto size = pixel_buffer.Length();

		byte* data = nullptr;
		winrt::check_hresult(buffer->Buffer(&data));

		return { reinterpret_cast<bgra8_t*>(data), size / sizeof(bgra8_t) };
	}

	template<typename T, typename U>
	static void span_memcpy(gsl::span<T> dst, gsl::span<U> src) {
		memcpy(dst.data(), src.data(), std::min(dst.size_bytes(), src.size_bytes()));
	}

	winrt::Windows::UI::Xaml::Controls::ScrollViewer m_root;
	winrt::Windows::UI::Xaml::Media::Imaging::WriteableBitmap m_bitmap{ nullptr };
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	winrt::Windows::UI::Xaml::Application::Start([](auto&&) {
		winrt::make<App>();
	});
}

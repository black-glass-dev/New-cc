"""Source-wiring regressions only; not Android UI/codec execution evidence."""
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]/'app/src/main/java/com/owncut/app'

class ColorRouteWiring(unittest.TestCase):
    def test_launcher_does_not_swallow_active_grade_before_details(self):
        code=(ROOT/'MainActivity.kt').read_text()
        launcher=code.split('private fun launchExportPicker()',1)[1].split('private fun launchExportDocumentPicker',1)[0]
        self.assertIn('startTimelineExport(null)',launcher)
        self.assertNotIn('TimelineRemuxExporter.makePlan',launcher)
        self.assertIn('requireShippingExportSupported(plan)',code)
        self.assertIn('confirmExperimentalExport()',code)

    def test_saved_grade_has_main_viewer_transport_and_cleanup_hooks(self):
        code=(ROOT/'MainActivity.kt').read_text()
        self.assertIn('MainGradePreview(this,parent,clip',code)
        self.assertIn('graded.seek(sourceMs',code)
        self.assertIn('mainGradePreview?.pause()',code)
        self.assertIn('mainGradePreview?.close();mainGradePreview=null',code)
        viewer=(ROOT/'MainGradePreview.kt').read_text()
        self.assertIn('video.update(clip.grade,false)',viewer)
        self.assertIn('loop=false',viewer)
        self.assertIn('parent.removeView(layer)',viewer)

    def test_reference_bitmap_removed_from_live_viewer(self):
        code=(ROOT/'ClipColorEditor.kt').read_text()
        self.assertIn('preview.visibility=android.view.View.GONE;preview.setImageDrawable(null)',code)
        self.assertIn('preview.setImageBitmap(referenceBitmap)',code)
        self.assertIn('PreviewColorPolicy.needsInterpretation',code)
        self.assertIn('if(missing)',code)

if __name__=='__main__': unittest.main()
